#define _CAMERA
#define _SCENEENVIRONMENT
#include "AtmosphereCommon.hlsli"

struct PushConstants
{
    uint minRaySteps;
    uint maxRaySteps;
};
ConstantBuffer< PushConstants > g_Push : register(b0, ROOT_CONSTANT_SPACE);

ConstantBuffer< DescriptorHeapIndex > g_TransmittanceLUT   : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MultiScatteringLUT : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutSkyViewLUT      : register(b3, ROOT_CONSTANT_SPACE);


float3 GetSkyViewRayDirectionFromUV(float2 uv, float viewHeight, float planetRadiusKm)
{
	float Vhorizon           = sqrt(viewHeight * viewHeight - planetRadiusKm * planetRadiusKm);
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
    AtmosphereData Atmosphere = GetAtmosphereData();

    Texture2D< float3 > TransmittanceLUT   = GetResource(g_TransmittanceLUT.index);
    Texture2D< float3 > MultiScatteringLUT = GetResource(g_MultiScatteringLUT.index);

    float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, Atmosphere.atmosphereRadiusKm);
    if (atmosphereIntersection.y < 0.0)
        return 0.0;
    
    float rayStart  = max(0.0, atmosphereIntersection.x);
    float rayLength = min(maxDistance, atmosphereIntersection.y) - rayStart;
    if (rayLength <= 0.0)
        return 0.0;

    // light illuminance
    float3 lightColor = float3(Atmosphere.light.colorR, Atmosphere.light.colorG, Atmosphere.light.colorB);
    if (Atmosphere.light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(Atmosphere.light.temperatureK);

    float3 E = Atmosphere.light.illuminanceLux * lightColor;

    // phase functions
    float3 sunDirection = float3(-Atmosphere.light.dirX, -Atmosphere.light.dirY, -Atmosphere.light.dirZ);

    float cosTheta      = dot(rayDir, sunDirection);
    float phaseRayleigh = RayleighPhase(cosTheta);
    float phaseMie      = MiePhase(cosTheta, Atmosphere.miePhaseG);

    // variable sampling count according to rayLength
    float numSteps = float(g_Push.maxRaySteps);// lerp(float(g_Push.minRaySteps), float(g_Push.maxRaySteps), clamp(rayLength / 150.0, 0.0, 1.0));
    float stepSize = rayLength / numSteps;

    float3 L          = 0.0;
    float3 throughput = 1.0;
    for (float i = 0.0; i < numSteps; i += 1.0)
    {
        float  t   = rayStart + (i + 0.5) * stepSize;
        float3 pos = rayOrigin + t * rayDir;
        
        // skip if below ground
        float viewHeight = length(pos);
        if (viewHeight < Atmosphere.planetRadiusKm)
            break;

        // skip if above atmosphere
        if (viewHeight > Atmosphere.atmosphereRadiusKm)
            continue;

        float sampleAltitude = viewHeight - Atmosphere.planetRadiusKm;
        
        // extinction(out-scattering) at sample point
        float rayleighDensity = GetDensityAtHeight(sampleAltitude, Atmosphere.rayleighDensityKm);
        float mieDensity      = GetDensityAtHeight(sampleAltitude, Atmosphere.mieDensityKm);
        float ozoneDensity    = GetDensityOzoneAtHeight(sampleAltitude, Atmosphere.ozoneCenterKm, Atmosphere.ozoneWidthKm);

        float3 rayleighScattering = Atmosphere.rayleighScattering * rayleighDensity;
        float  mieScattering      = Atmosphere.mieScattering * mieDensity;
        float  mieAbsorption      = Atmosphere.mieAbsorption * mieDensity;
        float3 ozoneAbsorption    = Atmosphere.ozoneAbsorption * ozoneDensity;
        
        float3 scattering        = rayleighScattering + mieScattering;
        float3 extinction        = rayleighScattering + (mieScattering + mieAbsorption) + ozoneAbsorption;
        float3 phasedScattering  = phaseRayleigh * rayleighScattering + phaseMie * mieScattering; // σs(x) * p(v,l)
        float3 stepTransmittance = exp(-extinction * stepSize);                                   // (2) T(xa,xb) = exp(−Integral(xa~xb, σ(x)dx))
        
        // transmittance from sample point to sun
        float  sampleHeight       = viewHeight;
        float  sampleTheta        = dot(normalize(pos), sunDirection);
        float3 transmittanceToSun = SampleTransmittanceLUT(TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, Atmosphere.planetRadiusKm, Atmosphere.atmosphereRadiusKm);

        // multi-scattering
        float2 msUV = clamp(
                        float2(sampleTheta * 0.5 + 0.5, inverseLerp(sampleHeight, Atmosphere.planetRadiusKm, Atmosphere.atmosphereRadiusKm)),
                    0.0, 1.0);
        float3 multiScattering = MultiScatteringLUT.SampleLevel(g_LinearClampSampler, msUV, 0).rgb;
        
        // planet shadow
        float2  planetIntersection = RaySphereIntersection(pos, sunDirection, PLANET_CENTER, Atmosphere.planetRadiusKm);
        float planetShadow       = planetIntersection.x < 0.0 ? 1.0 : 0.0;

        {
            // (4) S(x,li) = Vis(l_i) * T(x,x+t*li)
            // vec3 S = planetShadow * transmittanceToSun;
            // (3) Lscat(c,x,v) = σs(x) * (T(c,x) * S(x,l) * p(v,l) + Ψms) * E * dt
            // L     += (throughput * S * phasedScattering + multiScattering * scattering) * E * stepSize;

            // Analytical integration as proposed in slide 28 of http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
            float3 S    = (planetShadow * transmittanceToSun * phasedScattering + multiScattering * scattering) * E;
            float3 Sint = (S - S * stepTransmittance) / max(extinction, 1e-8);
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
    RWTexture2D< float3 > OutSkyViewLUT = GetResource(g_OutSkyViewLUT.index);

    uint2 texSize;
    OutSkyViewLUT.GetDimensions(texSize.x, texSize.y);
    if (tID.x >= texSize.x || tID.y >= texSize.y)
        return;

    float2 uv = float2(tID.xy + 0.5) / float2(texSize);

    AtmosphereData Atmosphere = GetAtmosphereData();

    float3 cameraPos =
        float3(g_Camera.posWORLD.x, max(g_Camera.posWORLD.y, MIN_VIEW_HEIGHT_ABOVE_GROUND), g_Camera.posWORLD.z);
    float3 cameraPosAbovePlanet =
        cameraPos * DISTANCE_SCALE + float3(0.0, Atmosphere.planetRadiusKm, 0.0);

    float  viewHeightWORLD = length(cameraPosAbovePlanet);
    float3 posWORLD        = float3(0.0, viewHeightWORLD, 0.0);

    float3 rayDir    = GetSkyViewRayDirectionFromUV(GetStretchedTextureUV(uv, float2(texSize)), viewHeightWORLD, Atmosphere.planetRadiusKm);
    float3 rayOrigin = posWORLD;
    
    float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, Atmosphere.planetRadiusKm);

    float maxDistance = RAY_MARCHING_MAX_DISTANCE;
    if (groundIntersection.x > 0.0)
        maxDistance = groundIntersection.x;

    float4 inscattered = RayMarchScattering(rayOrigin, rayDir, maxDistance);
    OutSkyViewLUT[tID.xy] = inscattered.rgb;
}