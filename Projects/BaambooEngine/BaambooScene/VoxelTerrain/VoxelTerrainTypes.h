#pragma once
#include "MathTypes.h"
#include "Primitives.h"
#include "EngineTypes.h"
#include "ShaderTypes.h"

namespace baamboo
{

// GPU density volume apron (extra samples each side)
constexpr u32 kVoxelDensityApron = 16u; // for covering upto LOD+3 in geomorphing

constexpr float kDefaultVoxelChunkWorldSizeMeter = 64.0f;
constexpr u32   kDefaultVoxelCellsPerAxis        = 128u;
constexpr u32   kDefaultVoxelSamplesPerAxis      = kDefaultVoxelCellsPerAxis + 1u;
constexpr float kDefaultVoxelSizeMeter           = kDefaultVoxelChunkWorldSizeMeter / (float)kDefaultVoxelCellsPerAxis;

// Erosion detail slice pool
constexpr u32 kMaxVoxelErosionSlices = 58u;

// Absolute terrain floor plane (below it is always air)
constexpr float kVoxelWorldFloorYMeter = 0.0f;

// Per-LOD-level page pools: class == LOD level
struct VoxelPageClass
{
    u32 triCapacity;
    u32 pageCount;
};
constexpr VoxelPageClass kVoxelPageClasses[] =
{
    { 220000u, 58u }, // LOD0
    { 175000u, 44u }, // LOD1
    { 140000u, 44u }, // LOD2
    { 105000u, 44u }, // LOD3
    {  85000u, 44u }, // LOD4
    {  85000u, 44u }, // LOD5
    {  85000u, 44u }, // LOD6
    {  85000u, 44u }, // LOD7
};
constexpr u32 kVoxelPageClassCount = 8u;

// pageID = classId(8b) << 24 | pageIdx(24b)
constexpr u32 MakeVoxelPageID(u32 classId, u32 idx) { return (classId << 24u) | idx; }
constexpr u32 VoxelPageClassId(u32 pageID)          { return pageID >> 24u; }
constexpr u32 VoxelPageIdx(u32 pageID)              { return pageID & 0x00FFFFFFu; }

// Per-class capacities: v = 3t, mv = 4t, mt = 4t/3, meshlets = t/12
constexpr u32 VoxelClassTriCap(u32 c)           { return kVoxelPageClasses[c].triCapacity; }
constexpr u32 VoxelClassPageCount(u32 c)        { return kVoxelPageClasses[c].pageCount; }
constexpr u32 VoxelClassVertexCap(u32 c)        { return VoxelClassTriCap(c) * 3u; }
constexpr u32 VoxelClassMeshletVertexCap(u32 c) { return VoxelClassTriCap(c) * 4u; }
constexpr u32 VoxelClassMeshletTriCap(u32 c)    { return VoxelClassTriCap(c) * 4u / 3u; }
constexpr u32 VoxelClassMeshletCap(u32 c)       { return VoxelClassTriCap(c) / 12u; }

// Pool bases: classes packed back-to-back, page idx strided by the class capacity
constexpr u32 VoxelClassPageBase(u32 c)         { u32 s = 0u; for (u32 i = 0u; i < c; ++i) s += VoxelClassPageCount(i); return s; }
constexpr u32 VoxelTotalPages()                 { return VoxelClassPageBase(kVoxelPageClassCount); }
constexpr u32 VoxelClassVertexBase(u32 c)       { u32 s = 0u; for (u32 i = 0u; i < c; ++i) s += VoxelClassPageCount(i) * VoxelClassVertexCap(i); return s; }
constexpr u32 VoxelClassMeshletVertexBase(u32 c){ u32 s = 0u; for (u32 i = 0u; i < c; ++i) s += VoxelClassPageCount(i) * VoxelClassMeshletVertexCap(i); return s; }
constexpr u32 VoxelClassMeshletTriBase(u32 c)   { u32 s = 0u; for (u32 i = 0u; i < c; ++i) s += VoxelClassPageCount(i) * VoxelClassMeshletTriCap(i); return s; }
constexpr u32 VoxelClassMeshletBase(u32 c)      { u32 s = 0u; for (u32 i = 0u; i < c; ++i) s += VoxelClassPageCount(i) * VoxelClassMeshletCap(i); return s; }
constexpr u32 VoxelTotalVertexPool()            { return VoxelClassVertexBase(kVoxelPageClassCount); }
constexpr u32 VoxelTotalMeshletVertexPool()     { return VoxelClassMeshletVertexBase(kVoxelPageClassCount); }
constexpr u32 VoxelTotalMeshletTriPool()        { return VoxelClassMeshletTriBase(kVoxelPageClassCount); }
constexpr u32 VoxelTotalMeshletPool()           { return VoxelClassMeshletBase(kVoxelPageClassCount); }

struct VoxelTerrainSettings
{
    float chunkWorldSizeMeter = kDefaultVoxelChunkWorldSizeMeter;
    u32   cellsPerAxis        = kDefaultVoxelCellsPerAxis;
    u32   samplesPerAxis      = kDefaultVoxelSamplesPerAxis;
    float voxelSizeMeter      = kDefaultVoxelSizeMeter;

    u32 maxLodLevel = 7u;

    float crossfadeSeconds = 0.2f; // swap dither-crossfade length (seconds)
    u32   debugFlags       = 0u;   // bit0 = chunk tint | bit1 = LOD tint

    // Procedural surface
    u32   seed              = 1337u;
    float frequency         = 0.015f; // base noise frequency
    u32   octaves           = 2u;
    float lacunarity        = 2.0f;
    float gain              = 0.5f;
    float warpStrength      = 0.0f;
    float warpFrequency     = 0.015f;
    float mountainAmplitude = 36.0f;  // peak-to-valley relief (m)
    float detailWeight      = 1.0f;
    float redistributionExp = 1.0f;
    float ridgedBlend       = 0.0f;
    float surfaceBaseYMeter = 32.0f;  // base surface height (m), world-absolute

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
