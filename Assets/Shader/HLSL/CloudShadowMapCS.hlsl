#define _SCENEENVIRONMENT
#include "AtmosphereCommon.hlsli"
#include "CloudCommon.hlsli"

struct CloudShadowData
{
    float4x4 mSunView;
    float4x4 mSunViewProj;
    float4x4 mSunViewProjInv;
};
ConstantBuffer< CloudShadowData > g_CloudShadow : register(b0, space1);

cbuffer PushConstant : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_NumLightRaymarchSteps;

    float    g_TimeSec;
    uint64_t g_Frame;
};

ConstantBuffer< DescriptorHeapIndex > g_OutCloudShadowMap : register(b5, ROOT_CONSTANT_SPACE);


// Reference: https://blog.selfshadow.com/publications/s2020-shading-course/hillaire/s2020_pbs_hillaire_slides.pdf
float3 RaymarchBSM(float3 rayOrigin, float3 rayDirection)
{
    CloudData      Cloud      = GetCloudData();
    AtmosphereData Atmosphere = GetAtmosphereData();

    float frontDepth = 1e30;
    float extinctionSum = 0.0;
    float extinctionCount = 0.0;
    float maxOpticalDepth = 0.0;

    bool bFirstHit = false;

    float rBottomLayer = Atmosphere.planetRadiusKm + Cloud.bottomLayerKm;

    float2 bottomIntersection = RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rBottomLayer);
    if (all(bottomIntersection < 0.0))
    {
        return float3(frontDepth, 0.0, 0.0);
    }
    float rayLength = bottomIntersection.x > 0.0 ? bottomIntersection.x : bottomIntersection.y;

    float ExtinctionStrength = (Cloud.extinctionStrength.r + Cloud.extinctionStrength.g + Cloud.extinctionStrength.b) / 3.0;
    ExtinctionStrength *= Cloud.extinctionScale;

    float3 offset = Cloud.windDirection * g_TimeSec * Cloud.windSpeedMps * 0.001;

    float numSteps = (float)g_NumLightRaymarchSteps;
    float stepSize = rayLength / numSteps;

    for (float i = 0.5; i < numSteps; i += 1.0)
    {
        float st = i * stepSize;
        float3 spos = rayOrigin + st * rayDirection;

        float saltitude = length(spos) - Atmosphere.planetRadiusKm;

        float shNorm = inverseLerp(saltitude, Cloud.bottomLayerKm, Cloud.topLayerKm);
        if (shNorm > 1.0 || shNorm < 0.0)
        {
            continue;
        }

        float stepDensity = SampleCloudDensity(spos, shNorm, offset, Cloud);
        if (stepDensity > 0.0)
        {
            if (!bFirstHit)
            {
                float4 sposVIEW = mul(g_CloudShadow.mSunView, float4(spos, 1.0));
                frontDepth = sposVIEW.z;

                bFirstHit = true;
            }

            float stepExtinction = stepDensity * ExtinctionStrength * 1000.0;

            extinctionSum   += stepExtinction;
            extinctionCount += 1.0;

            maxOpticalDepth += stepExtinction * stepSize;
        }
    }

    return float3(frontDepth, (extinctionCount > 0.0) ? (extinctionSum / extinctionCount) : 0.0, maxOpticalDepth);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    RWTexture2D< float3 > OutCloudShadowMap = GetResource(g_OutCloudShadowMap.index);

    uint width, height;
    OutCloudShadowMap.GetDimensions(width, height);
    int2 imgSize = int2(width, height);
    int2 pixCoords = int2(dispatchThreadID.xy);

    if (any(pixCoords >= imgSize))
        return;

    float2 uv = (float2(pixCoords) + 0.5) / float2(imgSize);

    // Reverse-Z
    float4 posNearCLIP  = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
    float4 posNearWORLD = mul(g_CloudShadow.mSunViewProjInv, posNearCLIP);

    float4 posFarCLIP  = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    float4 posFarWORLD = mul(g_CloudShadow.mSunViewProjInv, posFarCLIP);

    float3 rayOrigin    = posNearWORLD.xyz / posNearWORLD.w;
    float3 rayTarget    = posFarWORLD.xyz / posFarWORLD.w;
    float3 rayDirection = normalize(rayTarget - rayOrigin);

    float3 BSM = RaymarchBSM(rayOrigin, rayDirection);

    OutCloudShadowMap[pixCoords] = BSM;
}