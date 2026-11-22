#pragma once
#include "Types.h"

static const u32 kCullPhase1 = 0u;
static const u32 kCullPhase2 = 1u;

static const u32 kCullFlagTriangleBackface = 1u << 0;
static const u32 kCullFlagTriangleSubpixel = 1u << 1;
static const u32 kCullFlagMeshletFrustum   = 1u << 2;
static const u32 kCullFlagMeshletCone      = 1u << 3;
static const u32 kCullFlagMeshletOcclusion = 1u << 4;

// Packed draw ID: [31] drawn in phase 1 this frame, [30:24] LOD, [23:0] instance ID
static const u32 kDrawIDEarlyBit = 1u << 31;

struct InstanceCullingConstants
{
    u32 cullingPhase;
    u32 padding0;
    u32 padding1;
    u32 padding2;
};

struct HiZConstants
{
    u32 mip0Width;
    u32 mip0Height;
    u32 spdMipCount;
    u32 padding0;
};

struct GBufferConstants
{
    float2 viewport;
    u32    phase;
    u32    padding0;
};

#ifdef __cplusplus
static_assert(sizeof(InstanceCullingConstants) == 16);
static_assert(sizeof(GBufferConstants) == 16);
static_assert(sizeof(HiZConstants) == 16);
#endif
