#pragma once
#include "MathTypes.h"
#include "Primitives.h"

namespace baamboo
{

// GPU density volume apron (extra samples each side): central-difference normals + cross-chunk seams.
constexpr u32 kVoxelDensityApron = 2u;

constexpr float kDefaultVoxelChunkWorldSizeMeter = 64.0f;
constexpr u32   kDefaultVoxelCellsPerAxis        = 64u; // voxel size = chunkWorldSize / cellsPerAxis
constexpr u32   kDefaultVoxelSamplesPerAxis      = kDefaultVoxelCellsPerAxis + 1u;
constexpr float kDefaultVoxelSizeMeter           = kDefaultVoxelChunkWorldSizeMeter / (float)kDefaultVoxelCellsPerAxis;

struct VoxelTerrainSettings
{
    float chunkWorldSizeMeter = kDefaultVoxelChunkWorldSizeMeter;
    u32   cellsPerAxis        = kDefaultVoxelCellsPerAxis;
    u32   samplesPerAxis      = kDefaultVoxelSamplesPerAxis;
    float voxelSizeMeter      = kDefaultVoxelSizeMeter;
};

} // namespace baamboo
