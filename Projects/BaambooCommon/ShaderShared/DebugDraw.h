#pragma once
#include "Types.h"

static const u32 kDebugClusterFlagHeatmap   = 1u << 0;
static const u32 kDebugClusterFlagSkipEmpty = 1u << 1;

static const u32 kDebugLightBitSpot   = 0u;
static const u32 kDebugLightBitArea   = 1u;
static const u32 kDebugLightBitSphere = 2u;
static const u32 kDebugLightBitDisk   = 3u;
static const u32 kDebugLightBitTube   = 4u;

static const u32 kDebugLightMaxVertices = 192u;

struct DebugLineVertex
{
    float3 position;
    f32    padding0;
    float3 color;
    f32    alpha;
};

struct DebugFrustumConstants
{
    float3 lineColor;
    f32    lineAlpha;
};

struct DebugClusterConstants
{
    float3 lineColor;
    f32    lineAlpha;

    u32 numTilesX;
    u32 numTilesY;
    u32 numSlices;
    u32 flagsBits;

    u32 saturationMax;
    u32 padding0;
    u32 padding1;
    u32 padding2;
};

struct DebugLightConstants
{
    float3 lineColor;
    f32    lineAlpha;

    u32 typeMask;
    u32 padding0;
    u32 padding1;
    u32 padding2;
};

#ifdef __cplusplus
static_assert(sizeof(DebugLineVertex) == 32);
static_assert(sizeof(DebugFrustumConstants) == 16);
static_assert(sizeof(DebugClusterConstants) == 48);
static_assert(sizeof(DebugLightConstants) == 32);
#endif
