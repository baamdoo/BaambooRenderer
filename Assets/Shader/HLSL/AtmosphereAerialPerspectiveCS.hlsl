#define _CAMERA
#include "Common.hlsli"
#define _ATMOSPHERE
#include "AtmosphereCommon.hlsli"

Texture2D< float3 >   g_TransmittanceLUT     : register(t0);
Texture2D< float3 >   g_MultiScatteringLUT   : register(t1);
RWTexture3D< float4 > g_AerialPerspectiveLUT : register(u0);

SamplerState g_LinearClampSampler : register(SAMPLER_INDEX_LINEAR_CLAMP);

float4 ComputeAerialPerspective(float3 rayOrigin, float3 rayDir, float maxDistance, uint rayMarchSteps)
{
    float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Atmosphere.atmosphereRadius_km);
    if (atmosphereIntersection.y < 0.0)
        return 0.0;

    float rayStart  = max(0.0, atmosphereIntersection.x);
    float rayLength = min(maxDistance, atmosphereIntersection.y) - rayStart;
    if (rayLength <= 0.0)
        return 0.0;

    // light illuminance
    float3 lightColor = float3(g_Atmosphere.light.colorR, g_Atmosphere.light.colorG, g_Atmosphere.light.colorB);
    if (g_Atmosphere.light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(g_Atmosphere.light.temperature_K);

    float3 E = g_Atmosphere.light.illuminance_lux * lightColor;

    // phase functions
    float cosTheta      = dot(rayDir, float3(-g_Atmosphere.light.dirX, -g_Atmosphere.light.dirY, -g_Atmosphere.light.dirZ));
    float phaseRayleigh = RayleighPhase(cosTheta);
    float phaseMie      = MiePhase(cosTheta, g_Atmosphere.miePhaseG);

    float stepSize = rayLength / float(rayMarchSteps);

    float3 L          = 0.0; // In-scattered luminance
    float3 throughput = 1.0; // Transmittance
    for (uint i = 0u; i < rayMarchSteps; ++i) 
    {
        float  t   = (float(i) + 0.5) * stepSize;
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
        float  sampleTheta        = dot(normalize(pos), float3(-g_Atmosphere.light.dirX, -g_Atmosphere.light.dirY, -g_Atmosphere.light.dirZ));
        float3 transmittanceToSun = SampleTransmittanceLUT(g_TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km);

        // multi-scattering
        float2 msUV = clamp(
                        vec2(sampleTheta * 0.5 + 0.5, inverseLerp(sampleHeight, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km)),
                    0.0, 1.0);
        float3 multiScattering = g_MultiScatteringLUT.SampleLevel(g_LinearClampSampler, msUV, 0).rgb;

        // planet shadow
        float2 planetIntersection = RaySphereIntersection(pos, float3(-g_Atmosphere.light.dirX, -g_Atmosphere.light.dirY, -g_Atmosphere.light.dirZ), PLANET_CENTER, g_Atmosphere.planetRadius_km);
        float  planetShadow       = planetIntersection.x < 0.0 ? 1.0 : 0.0;

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

[numthreads(4, 4, 4)]
void main(uint3 tID : SV_DispatchThreadID)
{
    uint3 texSize;
    g_AerialPerspectiveLUT.GetDimensions(texSize.x, texSize.y, texSize.z);
    if (tID.x >= texSize.x || tID.y >= texSize.y || tID.z >= texSize.z)
        return;

    float3 uvw = float3(tID.xyz + 0.5) / float3(texSize);
    
    float3 cameraPos =
        float3(g_Camera.posWORLD.x, max(g_Camera.posWORLD.y, MIN_VIEW_HEIGHT_ABOVE_GROUND), g_Camera.posWORLD.z);
    float3 cameraPosAbovePlanet =
        cameraPos * DISTANCE_SCALE + float3(0.0, g_Atmosphere.planetRadius_km, 0.0);

    float slice = uvw.z;
    slice *= slice;
    slice *= float(texSize.z);

    float  maxDistance = AP_KM_PER_SLICE * slice;
    float3 rayDir      = normalize(ReconstructWorldPos(uvw.xy, 0.0, g_Camera.mViewProjInv));
    float3 rayOrigin   = cameraPosAbovePlanet;
    float3 posInFroxel = cameraPosAbovePlanet + rayDir * maxDistance;
    
    if (length(posInFroxel) < g_Atmosphere.planetRadius_km + EPSILON)
    {
        posInFroxel = normalize(posInFroxel) * (g_Atmosphere.planetRadius_km + EPSILON);
        rayDir      = normalize(posInFroxel - cameraPosAbovePlanet);
        maxDistance = length(posInFroxel - cameraPosAbovePlanet);
    }

    uint   rayMarchSteps = max(1, uint(tID.z + 1) * 2);
    float4 result        = ComputeAerialPerspective(rayOrigin, rayDir, maxDistance, rayMarchSteps);
    
    g_AerialPerspectiveLUT[tID.xyz] = result;
}