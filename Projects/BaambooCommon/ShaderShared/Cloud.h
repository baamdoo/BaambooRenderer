#pragma once
#include "Types.h"

struct CloudData
{
    float topLayerKm;
    float bottomLayerKm;
    float localOvercast;
    float shadowTracingDistanceKm;

    float cloudsScale;
    float clumpsVariation;
    float floorVariationClear;
    float floorVariationCloudy;

    float3 baseErosionScale;
    float  baseDensity;

    float baseErosionStrength;
    float baseErosionPower;
    float hfErosionStrength;
    float hfErosionDistortion;

    float groundContributionStrength;
    float extinctionScale;
    float msContribution;
    float msOcclusion;

    float3 windDirection;
    float  windSpeedMps;

    float ambientIntensity;
    float padding0;
    float padding1;
    float padding2;
};

struct CloudShadowData
{
    mat4 mSunView;
    mat4 mSunViewInv;
    mat4 mSunViewProj;
    mat4 mSunViewProjInv;
};

struct CloudShadowMapConstants
{
    mat4 mSunViewProjInv;

    u32 numLightRaymarchSteps;
    u32 padding0;
    u32 padding1;
    u32 padding2;
};

struct CloudRaymarchConstants
{
    u32 numCloudRaymarchSteps;
    u32 numLightRaymarchSteps;
    u32 frameIndex;
    u32 padding0;
};

struct CloudTemporalUprezConstants
{
    f32 blendAlpha;
    u32 historyValid;
    u32 padding0;
    u32 padding1;
};

#ifdef __cplusplus
static_assert(sizeof(CloudData) == 112);
static_assert(sizeof(CloudShadowMapConstants) == 80);
static_assert(sizeof(CloudRaymarchConstants) == 16);
static_assert(sizeof(CloudTemporalUprezConstants) == 16);
#endif
