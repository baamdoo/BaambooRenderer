#include "ShaderShared/Scene.h"
#include "Common/Atmosphere.slangh"

struct AtmosphereDistantSkyLightPush
{
    AtmosphereRaymarchConstants constants;

    uint ID_Scene;
    uint ID_TransmittanceLUT;
    uint ID_MultiScatteringLUT;
    uint ID_OutAtmosphereAmbientLUT;
};

[[vk::push_constant]] ConstantBuffer< AtmosphereDistantSkyLightPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Cosine-weighted directions around +Y: stratified elevation, golden-angle azimuth
float3 GetCosineHemisphereDirection(uint i, uint numSamples)
{
    const float goldenRatio = (1.0 + sqrt(5.0)) / 2.0;

    const float phi      = 2.0 * PI * frac(float(i) / goldenRatio);
    const float cosTheta = sqrt(1.0 - (float(i) + 0.5) / float(numSamples));
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    return float3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
}

[numthreads(64, 1, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture1D< float3 > OutAtmosphereAmbientLUT = GetResource(g_Push.ID_OutAtmosphereAmbientLUT);

    uint texSize;
    OutAtmosphereAmbientLUT.GetDimensions(texSize);
    if (tID.x >= texSize)
        return;

    StructuredBuffer< SceneGlobals > sceneBuf = GetResource(g_Push.ID_Scene);
    const AtmosphereData atmosphere = sceneBuf[0].environment.atmosphere;

    Texture2D< float3 > TransmittanceLUT   = GetResource(g_Push.ID_TransmittanceLUT);
    Texture2D< float3 > MultiScatteringLUT = GetResource(g_Push.ID_MultiScatteringLUT);

    const float  altitude  = (float(tID.x) + 0.5) / float(texSize) * (atmosphere.atmosphereRadiusKm - atmosphere.planetRadiusKm);
    const float3 rayOrigin = float3(0.0, atmosphere.planetRadiusKm + altitude, 0.0);

    const uint  numSamples = g_Push.constants.msIsoSampleCount;
    const float minSteps   = float(g_Push.constants.svMinRaySteps);
    const float maxSteps   = float(g_Push.constants.svMaxRaySteps);

    // Every direction points above the local horizon, so no ray reaches the ground
    float3 inScatteredSum = float3(0.0);
    for (uint i = 0u; i < numSamples; ++i)
    {
        const float3 rayDir = GetCosineHemisphereDirection(i, numSamples);

        float rayStart, rayLength;
        if (!GetAtmosphereRaySegment(atmosphere, rayOrigin, rayDir, RAY_MARCHING_MAX_DISTANCE, rayStart, rayLength))
            continue;

        const uint numSteps = uint(ceil(lerp(minSteps, maxSteps, saturate(rayLength / 150.0))));
        inScatteredSum += IntegrateAtmosphereInScattering(
            atmosphere, TransmittanceLUT, MultiScatteringLUT,
            rayOrigin, rayDir, rayStart, rayLength, numSteps).rgb;
    }

    // pdf = cos / PI, so E = (1 / N) * sum(L * cos / pdf) = (PI / N) * sum(L)
    const float3 irradiance = numSamples > 0u ? inScatteredSum * (PI / float(numSamples)) : float3(0.0);
    OutAtmosphereAmbientLUT[tID.x] = irradiance;
}
