#pragma once
#include "Boundings.h"
#include "MathTypes.h"
#include "Primitives.h"

#include <functional>

namespace baamboo
{


constexpr float kDefaultVoxelChunkWorldSizeMeter = 64.0f;
constexpr u32   kDefaultVoxelCellsPerAxis        = 32u;
constexpr u32   kDefaultVoxelSamplesPerAxis      = kDefaultVoxelCellsPerAxis + 1u;
constexpr float kDefaultVoxelSizeMeter           = kDefaultVoxelChunkWorldSizeMeter / (float)kDefaultVoxelCellsPerAxis;

struct VoxelTerrainSettings
{
    float chunkWorldSizeMeter = kDefaultVoxelChunkWorldSizeMeter;
    u32   cellsPerAxis        = kDefaultVoxelCellsPerAxis;
    u32   samplesPerAxis      = kDefaultVoxelSamplesPerAxis;
    float voxelSizeMeter      = kDefaultVoxelSizeMeter;
};

struct VoxelChunkCoord
{
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;
};

struct VoxelTerrainChunkDesc
{
    float3               originWorld = float3(0.0f);
    VoxelTerrainSettings settings    = {};
    BoundingBox          worldBounds = BoundingBox(float3(0.0f), float3(0.0f));

    std::function< float(const float3&) > SDF;
};


} // namespace baamboo
