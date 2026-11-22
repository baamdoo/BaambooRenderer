#pragma once
#include "Types.h"

struct BB_GPU_ALIGN(16) PathTracerConstants
{
    u32 accumulatedSampleCount;
    u32 samplesPerFrame;
    u32 bResetAccumulation;
    u32 maxDepth;

    float3 environmentRadiance;
    u32    bUseEnvironmentMap;

    u32 bUseEnvironmentSampling;
    u32 environmentDistributionWidth;
    u32 environmentDistributionHeight;
    u32 sobolSeed;

    float3 sceneBoundsCenter;
    f32    sceneBoundsRadius;
};

#ifdef __cplusplus
static_assert(sizeof(PathTracerConstants) == 64);
#endif
