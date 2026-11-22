#include "ShaderShared/Scene.h"
#include "Common/Binding.slangh"
#include "Common/Math.slangh"
#include "Common/Atmosphere.slangh"
#include "Common/Cloud.slangh"

struct CloudShadowMapPush
{
    CloudShadowMapConstants constants;

    uint ID_Scene;
    uint ID_CloudMacroMap;
    uint ID_CloudProfileLUT;
    uint ID_CloudBaseNoise;
    uint ID_OutCloudShadowMap;
};

[[vk::push_constant]] ConstantBuffer< CloudShadowMapPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Beer shadow map: (front depth, mean extinction, max optical depth), all in meters
// Reference: https://blog.selfshadow.com/publications/s2020-shading-course/hillaire/s2020_pbs_hillaire_slides.pdf
float3 RaymarchBSM(CloudVolume clouds, float planetRadiusKm, float3 rayOrigin, float3 rayDirection)
{
    const float topLayerMeter     = clouds.data.topLayerKm * KM_TO_M;
    const float bottomLayerMeter  = clouds.data.bottomLayerKm * KM_TO_M;
    const float planetRadiusMeter = planetRadiusKm * KM_TO_M;

    const float  noHitDepth = 1e30;
    const float3 noHit      = float3(noHitDepth, 0.0, 0.0);

    float2 topIntersection;
    float2 bottomIntersection;
    const bool bTop    = RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, planetRadiusMeter + topLayerMeter, topIntersection);
    const bool bBottom = RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, planetRadiusMeter + bottomLayerMeter, bottomIntersection);
    if (!bTop)
        return noHit;

    const float rayStart = topIntersection.x;
    float       rayEnd   = bottomIntersection.x;
    if (!bBottom)
    {
        if (topIntersection.y <= 0.0)
            return noHit;
        rayEnd = topIntersection.y;
    }
    if (rayStart > rayEnd)
        return noHit;

    const uint  numSteps = g_Push.constants.numLightRaymarchSteps;
    const float stepSize = (rayEnd - rayStart) / float(numSteps);

    float frontDepth      = noHitDepth;
    float extinctionSum   = 0.0;
    float extinctionCount = 0.0;
    float maxOpticalDepth = 0.0;

    const float3 marchOrigin = rayOrigin + rayStart * rayDirection;
    for (uint i = 0; i < numSteps; ++i)
    {
        const float  st   = (float(i) + 0.5) * stepSize;
        const float3 spos = marchOrigin + st * rayDirection;

        const float saltitude = length(spos) - planetRadiusMeter;
        const float shNorm    = inverseLerp(saltitude, bottomLayerMeter, topLayerMeter);
        if (shNorm > 1.0 || shNorm < 0.0)
            continue;

        const float3 sposInLayer         = float3(spos.x, saltitude, spos.z);
        const float3 conservativeDensity = SampleCloudConservativeDensity(clouds, sposInLayer, shNorm);
        if (conservativeDensity.x <= 0.0)
            continue;

        const float stepExtinction = SampleCloudExtinction(clouds, sposInLayer, conservativeDensity, false).extinction;
        if (stepExtinction > 0.0)
        {
            if (frontDepth == noHitDepth)
                frontDepth = st;

            extinctionSum   += stepExtinction;
            extinctionCount += 1.0;
            maxOpticalDepth += stepExtinction * stepSize;
        }
    }

    return float3(frontDepth, extinctionCount > 0.0 ? extinctionSum / extinctionCount : 0.0, maxOpticalDepth);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    RWTexture2D< float3 > OutCloudShadowMap = GetResource(g_Push.ID_OutCloudShadowMap);

    uint2 imageSize;
    OutCloudShadowMap.GetDimensions(imageSize.x, imageSize.y);
    if (any(dispatchThreadID.xy >= imageSize))
        return;

    StructuredBuffer< SceneGlobals > scene = GetResource(g_Push.ID_Scene);

    CloudVolume clouds;
    clouds.data       = scene[0].environment.cloud;
    clouds.macroMap   = GetResource(g_Push.ID_CloudMacroMap);
    clouds.baseNoise  = GetResource(g_Push.ID_CloudBaseNoise);
    clouds.profileLUT = GetResource(g_Push.ID_CloudProfileLUT);

    // Same uv convention as every screen pass (y down), so a reader maps with mSunViewProj the usual way
    const float2 uv  = (float2(dispatchThreadID.xy) + 0.5) / float2(imageSize);
    const float2 ndc = float2(uv.x * 2.0 - 1.0, uv.y * -2.0 + 1.0);

    // Reverse-Z: the sun-side plane is at depth 1
    const float4 posNearWS = mul(g_Push.constants.mSunViewProjInv, float4(ndc, 1.0, 1.0));
    const float4 posFarWS  = mul(g_Push.constants.mSunViewProjInv, float4(ndc, 0.0, 1.0));

    const float3 rayOrigin    = posNearWS.xyz / posNearWS.w;
    const float3 rayDirection = normalize(posFarWS.xyz / posFarWS.w - rayOrigin);

    OutCloudShadowMap[dispatchThreadID.xy] = RaymarchBSM(clouds, scene[0].environment.atmosphere.planetRadiusKm, rayOrigin, rayDirection);
}
