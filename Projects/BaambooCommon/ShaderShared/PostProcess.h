#pragma once
#include "Types.h"

struct ToneMappingConstants
{
    u32 tonemapOp;     // 0: None, 1: Reinhard, 2: ACES, 3: Uncharted2, 4: Uchimura
    f32 ev100;
    f32 gamma;
    f32 bloomStrength;
};

struct BloomDownsampleConstants
{
    u32 bFirstDownsample;
    u32 padding0;
    u32 padding1;
    u32 padding2;
};

struct BloomUpsampleConstants
{
    f32 filterRadius;  // in low-mip texels
    u32 padding0;
    u32 padding1;
    u32 padding2;
};

struct TemporalAntiAliasingConstants
{
    f32 blendFactor;
    u32 bFirstFrame;
    u32 padding0;
    u32 padding1;
};

struct SharpenConstants
{
    f32 sharpness;
    u32 padding0;
    u32 padding1;
    u32 padding2;
};

#ifdef __cplusplus
static_assert(sizeof(ToneMappingConstants) == 16);
static_assert(sizeof(BloomDownsampleConstants) == 16);
static_assert(sizeof(BloomUpsampleConstants) == 16);
static_assert(sizeof(TemporalAntiAliasingConstants) == 16);
static_assert(sizeof(SharpenConstants) == 16);
#endif
