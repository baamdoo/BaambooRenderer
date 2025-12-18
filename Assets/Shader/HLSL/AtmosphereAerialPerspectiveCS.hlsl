#define _CAMERA
#define _SCENEENVIRONMENT
#include "AtmosphereCommon.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_TransmittanceLUT        : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MultiScatteringLUT      : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutAerialPerspectiveLUT : register(b3, ROOT_CONSTANT_SPACE);


float4 ComputeAerialPerspective(float3 rayOrigin, float3 rayDir, float maxDistance, uint rayMarchSteps)
{
    AtmosphereData Atmosphere = GetAtmosphereData();

    Texture2D< float3 > TransmittanceLUT   = GetResource(g_TransmittanceLUT.index);
    Texture2D< float3 > MultiScatteringLUT = GetResource(g_MultiScatteringLUT.index);

    float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, Atmosphere.atmosphereRadiusKm);
    if (atmosphereIntersection.y < 0.0)
        return float4(0.0, 0.0, 0.0, 1.0);

    float rayStart  = max(0.0, atmosphereIntersection.x);
    float rayLength = min(maxDistance, atmosphereIntersection.y) - rayStart;
    if (rayLength <= 0.0)
        return float4(0.0, 0.0, 0.0, 1.0);

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

    float stepSize = rayLength / float(rayMarchSteps);

    float3 L          = 0.0; // In-scattered luminance
    float3 throughput = 1.0; // Transmittance
    for (uint i = 0u; i < rayMarchSteps; ++i) 
    {
        float  t   = (float(i) + 0.5) * stepSize;
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
        float2 planetIntersection = RaySphereIntersection(pos, sunDirection, PLANET_CENTER, Atmosphere.planetRadiusKm);
        float  planetShadow       = planetIntersection.x < 0.0 ? 1.0 : 0.0;

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

[numthreads(4, 4, 4)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture3D< float4 > OutAerialPerspectiveLUT = GetResource(g_OutAerialPerspectiveLUT.index);

    uint3 texSize;
    OutAerialPerspectiveLUT.GetDimensions(texSize.x, texSize.y, texSize.z);
    if (tID.x >= texSize.x || tID.y >= texSize.y || tID.z >= texSize.z)
        return;

    float3 uvw = float3(tID.xyz + 0.5) / float3(texSize);

    AtmosphereData Atmosphere = GetAtmosphereData();

    float3 cameraPos =
        float3(g_Camera.posWORLD.x, max(g_Camera.posWORLD.y, MIN_VIEW_HEIGHT_ABOVE_GROUND), g_Camera.posWORLD.z);
    float3 cameraPosAbovePlanet =
        cameraPos * DISTANCE_SCALE + float3(0.0, Atmosphere.planetRadiusKm, 0.0);

    float slice = uvw.z;
    slice *= slice;
    slice *= float(texSize.z);

    float  maxDistance = AP_KM_PER_SLICE * slice;
    float3 rayDir      = normalize(ReconstructWorldPos(uvw.xy, 0.0, g_Camera.mViewProjInv));
    float3 rayOrigin   = cameraPosAbovePlanet;
    float3 posInFroxel = cameraPosAbovePlanet + rayDir * maxDistance;
    
    if (length(posInFroxel) < Atmosphere.planetRadiusKm + EPSILON)
    {
        posInFroxel = normalize(posInFroxel) * (Atmosphere.planetRadiusKm + EPSILON);
        rayDir      = normalize(posInFroxel - cameraPosAbovePlanet);
        maxDistance = length(posInFroxel - cameraPosAbovePlanet);
    }

    uint   rayMarchSteps = max(1, uint(tID.z + 1) * 2);
    float4 result        = ComputeAerialPerspective(rayOrigin, rayDir, maxDistance, rayMarchSteps);
    
    OutAerialPerspectiveLUT[tID.xyz] = result;
}