#define _CAMERA
#include "Common.hlsli"
#define _ATMOSPHERE
#include "AtmosphereCommon.hlsli"

Texture2D< float3 >   g_TransmittanceLUT   : register(t0);
Texture2D< float3 >   g_MultiScatteringLUT : register(t1);
RWTexture2D< float3 > g_SkyViewLUT         : register(u0);

SamplerState g_LinearClampSampler : register(s0);

struct PushConstants
{
    uint minRaySteps;
    uint maxRaySteps;
};
ConstantBuffer< PushConstants > g_Push : register(b0, ROOT_CONSTANT_SPACE);

static const float RAY_MARCHING_MAX_DISTANCE = 1e6;

float3 GetSkyViewRayDirectionFromUV(float2 uv, float viewHeight)
{
	float Vhorizon           = sqrt(viewHeight * viewHeight - g_Atmosphere.planetRadius_km * g_Atmosphere.planetRadius_km);
	float cosBeta            = Vhorizon / viewHeight;				
	float beta               = acosFast4(cosBeta);
	float zenithHorizonAngle = PI - beta;

	float latitude;
	if (uv.y < 0.5)
	{
		float coord = 1.0 - 2.0 * uv.y;
		coord      *= coord;
		coord       = 1.0 - coord;

		latitude = zenithHorizonAngle * coord;
	}
	else
	{
		float coord = uv.y * 2.0 - 1.0;
		coord      *= coord;

		latitude = zenithHorizonAngle + beta * coord;
	}

	float longitude = uv.x * 2.0 * PI + PI; // '+PI' to resolve 180 degrees mis-unligned between texture coordinate and spheric coordinate

	float cosLatitude  = cos(latitude);
    float sinLatitude  = sin(latitude);
	float cosLongitude = cos(longitude);
    float sinLongitude = sin(longitude);
    float3 viewDir = float3(
		    sinLatitude * cosLongitude,
		    cosLatitude,
		    sinLatitude * sinLongitude
		);

    return viewDir;
}

float4 RayMarchScattering(float3 rayOrigin, float3 rayDir, float maxDistance)
{
    float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Atmosphere.atmosphereRadius_km);
    if (atmosphereIntersection.y < 0.0)
        return 0.0;
    
    float rayStart  = max(0.0, atmosphereIntersection.x);
    float rayLength = min(maxDistance, atmosphereIntersection.y) - rayStart;
    if (rayLength <= 0.0)
        return 0.0;

    // light illuminance
    float3 lightColor = g_Atmosphere.light.color;
    if (g_Atmosphere.light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(g_Atmosphere.light.temperature_K);

    float3 E = g_Atmosphere.light.illuminance_lux * lightColor;

    // phase functions
    float cosTheta      = dot(rayDir, -g_Atmosphere.light.dir);
    float phaseRayleigh = RayleighPhase(cosTheta);
    float phaseMie      = MiePhase(cosTheta, g_Atmosphere.miePhaseG);

    // variable sampling count according to rayLength
    float numSteps = lerp(float(g_Push.minRaySteps), float(g_Push.maxRaySteps), clamp(rayLength / 150.0, 0.0, 1.0));
    float stepSize = rayLength / numSteps;

    float3 L          = 0.0;
    float3 throughput = 1.0;
    for (uint i = 0u; i < numSteps; ++i) 
    {
        float  t   = rayStart + (float(i) + 0.5) * stepSize;
        float3 pos = rayOrigin + t * rayDir;
        
        // skip if below ground
        float viewHeight = length(pos);
        if (viewHeight < g_Atmosphere.planetRadius_km) 
            break;

        // skip if above atmosphere
        if (viewHeight > g_Atmosphere.atmosphereRadius_km)
            continue;

        float sampleAltitude = viewHeight - g_Atmosphere.planetRadius_km;
        
        // extinction(out-scattering) at sample point
        float rayleighDensity = GetDensityAtHeight(sampleAltitude, g_Atmosphere.rayleighDensityH_km);
        float mieDensity      = GetDensityAtHeight(sampleAltitude, g_Atmosphere.mieDensityH_km);
        float ozoneDensity    = GetDensityOzoneAtHeight(sampleAltitude);

        float3 rayleighScattering = g_Atmosphere.rayleighScattering * rayleighDensity;
        float  mieScattering      = g_Atmosphere.mieScattering * mieDensity;
        float  mieAbsorption      = g_Atmosphere.mieAbsorption * mieDensity;
        float3 ozoneAbsorption    = g_Atmosphere.ozoneAbsorption * ozoneDensity;
        
        float3 scattering        = rayleighScattering + mieScattering;
        float3 extinction        = rayleighScattering + (mieScattering + mieAbsorption) + ozoneAbsorption;
        float3 phasedScattering  = phaseRayleigh * rayleighScattering + phaseMie * mieScattering; // σs(x) * p(v,l)
        float3 stepTransmittance = exp(-extinction * stepSize);                                   // (2) T(xa,xb) = exp(−Integral(xa~xb, σ(x)dx))
        
        // transmittance from sample point to sun
        float  sampleHeight       = viewHeight;
        float  sampleTheta        = dot(normalize(pos), -g_Atmosphere.light.dir);
        float3 transmittanceToSun = SampleTransmittanceLUT(g_TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km);

        // multi-scattering
        float2 msUV = clamp(
                        vec2(sampleTheta * 0.5 + 0.5, inverseLerp(sampleHeight, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km)),
                    0.0, 1.0);
        float3 multiScattering = g_MultiScatteringLUT.SampleLevel(g_LinearClampSampler, msUV, 0).rgb;
        
        // planet shadow
        float2  planetIntersection = RaySphereIntersection(pos, -g_Atmosphere.light.dir, PLANET_CENTER, g_Atmosphere.planetRadius_km);
        float planetShadow       = planetIntersection.x < 0.0 ? 1.0 : 0.0;

        {
            // (4) S(x,li) = Vis(l_i) * T(x,x+t*li)
            // vec3 S = planetShadow * transmittanceToSun;
            // (3) Lscat(c,x,v) = σs(x) * (T(c,x) * S(x,l) * p(v,l) + Ψms) * E * dt
            // L     += (throughput * S * phasedScattering + multiScattering * scattering) * E * stepSize;

            // Analytical integration as proposed in slide 28 of http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
            float3 S    = (planetShadow * transmittanceToSun * phasedScattering + multiScattering * scattering) * E;
            float3 Sint = (S - S * stepTransmittance) / extinction;
            L          += throughput * Sint;
        }
        throughput *= stepTransmittance;
    }
    
    // xyz: (3) Lscat(c,x,v), w: (1) T(c,p) (Lo will be evaluated later stage)
    return float4(L, dot(throughput, 1.0 / 3.0));
}

[numthreads(8, 8, 1)]
void main(uint2 tID : SV_DispatchThreadID)
{
    uint2 texSize;
    g_SkyViewLUT.GetDimensions(texSize.x, texSize.y);
    if (tID.x >= texSize.x || tID.y >= texSize.y)
        return;

    float2 uv = float2(tID.xy + 0.5) / float2(texSize);
    
    float3 cameraPos =
        float3(g_Camera.posWORLD.x, max(g_Camera.posWORLD.y, MIN_VIEW_HEIGHT_ABOVE_GROUND), g_Camera.posWORLD.z);
    float3 cameraPosAbovePlanet =
        cameraPos * DISTANCE_SCALE + float3(0.0, g_Atmosphere.planetRadius_km, 0.0);

    float  viewHeightWORLD = length(cameraPosAbovePlanet);
    float3 posWORLD        = vec3(0.0, viewHeightWORLD, 0.0);

    float3 rayDir    = GetSkyViewRayDirectionFromUV(GetStretchedTextureUV(uv, vec2(texSize)), viewHeightWORLD);
    float3 rayOrigin = posWORLD;
    
    float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Atmosphere.planetRadius_km);

    float maxDistance = RAY_MARCHING_MAX_DISTANCE;
    if (groundIntersection.x > 0.0)
        maxDistance = groundIntersection.x;

    float4 inscattered = RayMarchScattering(rayOrigin, rayDir, maxDistance);
    g_SkyViewLUT[tID.xy] = inscattered.rgb;
}