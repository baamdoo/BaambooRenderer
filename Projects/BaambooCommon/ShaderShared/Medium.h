#pragma once
#include "Types.h"

static const u32 MEDIUM_FLAG_AIR_CONTRIBUTOR = 1u << 0u;

// Slang applies these defaults only to `MediumData m = {};`; a bare `MediumData m;` stays uninitialized there.
struct BB_GPU_ALIGN(16) MediumData
{
    mat4 mWorldToDensity = kIdentity4x4;

    float3 sigmaA = float3(0.0f);
    float  densityScale = 1.0f;
    float3 sigmaS = float3(0.0f);
    float  phaseG = 0.0f;

    u32   densityByteOffset = kInvalidIndex;
    float ior               = 1.0f;
    float majorant          = 0.0f; // Upper bound on every RGB extinction channel, per world length.
    u32   mediumFlags       = 0u;   // Zero retains the existing replacement-medium behavior.
};

static const u32 kPrimaryRayMediumStackCapacity = 8u;

static const u32 PRIMARY_RAY_MEDIUM_SEED_STATUS_UNINITIALIZED      = 0u;
static const u32 PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID              = 1u;
static const u32 PRIMARY_RAY_MEDIUM_SEED_STATUS_INVALID_PARAMS     = 2u;
static const u32 PRIMARY_RAY_MEDIUM_SEED_STATUS_TRACE_FAILED       = 3u;
static const u32 PRIMARY_RAY_MEDIUM_SEED_STATUS_HIT_LIMIT          = 4u;
static const u32 PRIMARY_RAY_MEDIUM_SEED_STATUS_CONSENSUS_MISMATCH = 5u;

struct BB_GPU_ALIGN(16) PrimaryMediumQueryParams
{
    float3 sceneBoundsCenter;
    f32    sceneBoundsRadius;

    u32 worldExteriorMediumID;
    f32 worldExteriorIOR;
    u32 maxTraceHits;
    u32 padding0;
};

struct BB_GPU_ALIGN(16) PrimaryRayMediumSeedEntryData
{
    u32 boundaryInstanceID;
    u32 mediumID;
    f32 ior;
    u32 padding0;
};

struct BB_GPU_ALIGN(16) PrimaryRayMediumStackSeedData
{
    u32 status;
    u32 count;
    u32 airCount;
    u32 padding0;

    PrimaryRayMediumSeedEntryData entries[kPrimaryRayMediumStackCapacity];
    PrimaryRayMediumSeedEntryData airEntries[kPrimaryRayMediumStackCapacity];
};

#ifdef __cplusplus
static_assert(sizeof(MediumData) == 112);
static_assert(alignof(MediumData) == 16);
static_assert(sizeof(PrimaryMediumQueryParams) == 32);
static_assert(sizeof(PrimaryRayMediumSeedEntryData) == 16);
static_assert(sizeof(PrimaryRayMediumStackSeedData) == 272);
static_assert(alignof(PrimaryRayMediumStackSeedData) == 16);
#endif
