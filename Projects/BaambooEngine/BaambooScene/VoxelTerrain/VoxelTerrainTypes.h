#pragma once
#include "MathTypes.h"
#include "Primitives.h"
#include "EngineTypes.h"

namespace baamboo
{

// GPU density volume apron (extra samples each side)
constexpr u32 kVoxelDensityApron = 2u;

constexpr float kDefaultVoxelChunkWorldSizeMeter = 128.0f;
constexpr u32   kDefaultVoxelCellsPerAxis        = 256u;
constexpr u32   kDefaultVoxelSamplesPerAxis      = kDefaultVoxelCellsPerAxis + 1u;
constexpr float kDefaultVoxelSizeMeter           = kDefaultVoxelChunkWorldSizeMeter / (float)kDefaultVoxelCellsPerAxis;

struct VoxelTerrainSettings
{
    float chunkWorldSizeMeter = kDefaultVoxelChunkWorldSizeMeter;
    u32   cellsPerAxis        = kDefaultVoxelCellsPerAxis;
    u32   samplesPerAxis      = kDefaultVoxelSamplesPerAxis;
    float voxelSizeMeter      = kDefaultVoxelSizeMeter;

    // Procedural surface
    u32   seed              = 1337u;
    float frequency         = 0.015f; // base noise frequency
    u32   octaves           = 6u;
    float lacunarity        = 2.0f;
    float gain              = 0.5f;
    float warpStrength      = 0.0f;
    float warpFrequency     = 0.015f;
    float mountainAmplitude = 36.0f;  // peak-to-valley relief (m)
    float detailWeight      = 1.0f;
    float redistributionExp = 1.0f;
    float ridgedBlend       = 0.0f;
    float surfaceLevelRatio = 0.5f;   // base surface height as chunk fraction (0..1)

    // Erosion
    float erosionScale         = 32.0f; // largest gully wavelength (m)
    float erosionStrength      = 0.22f;
    float erosionGullyWeight   = 0.5f;
    float erosionDetail        = 1.5f;
    float erosionOnsetInput    = 1.25f;
    float erosionOnsetOctave   = 1.25f;
    float erosionCellScale     = 0.7f;
    float erosionNormalization = 0.5f;
    float erosionSlopeScale    = 1.0f;
    u32   erosionOctaves       = 8u;

    VoxelDiceSettings dice;
};

} // namespace baamboo
