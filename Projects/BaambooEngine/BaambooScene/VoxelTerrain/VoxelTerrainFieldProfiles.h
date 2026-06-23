#pragma once
#include "VoxelTerrainDebug.h"

struct VoxelTerrainComponent;

namespace baamboo
{

enum class VoxelTerrainHeightFieldShape
{
    Constant,
    Plane,
    Periodic,
};

struct VoxelTerrainHeightFieldParameters
{
    VoxelTerrainHeightFieldShape shape = VoxelTerrainHeightFieldShape::Constant;
    float baseHeightMeter = 0.0f;
    float slopeX = 0.0f;
    float slopeZ = 0.0f;
    float anchorXMeter = 0.0f;
    float anchorZMeter = 0.0f;
    float amplitudeMeter = 0.0f;
    float wavelengthXMeter = 1.0f;
    float wavelengthZMeter = 1.0f;
};

struct VoxelTerrainFieldProfile
{
    VoxelTerrainFieldPreset preset = VoxelTerrainFieldPreset::SphereRegression;
    const char* displayName = nullptr;
    VoxelTerrainSDFDistanceSemantics distanceSemantics = VoxelTerrainSDFDistanceSemantics::DistanceLike;
    bool bExpectClosedSurface = false;
    bool bIsGraphSurface = false;
};

u32 GetVoxelTerrainFieldPresetCount();
VoxelTerrainFieldPreset GetVoxelTerrainFieldPresetAt(u32 index);
bool TryGetVoxelTerrainFieldPresetIndex(VoxelTerrainFieldPreset preset, i32& outIndex);

const VoxelTerrainFieldProfile& GetVoxelTerrainFieldProfile(VoxelTerrainFieldPreset preset);
const char* GetVoxelTerrainFieldPresetName(VoxelTerrainFieldPreset preset);
const char* GetVoxelTerrainDistanceSemanticsName(VoxelTerrainSDFDistanceSemantics semantics);
const char* GetVoxelTerrainFieldResidualUnit(VoxelTerrainSDFDistanceSemantics semantics);

const VoxelTerrainHeightFieldParameters* GetVoxelTerrainHeightFieldParameters(VoxelTerrainFieldPreset preset);
const char* GetVoxelTerrainHeightFieldShapeName(VoxelTerrainHeightFieldShape shape);

float EvaluateVoxelTerrainHeightFieldHeight(
    const VoxelTerrainHeightFieldParameters& params,
    float xWorld,
    float zWorld);
float EvaluateVoxelTerrainHeightFieldSignedField(
    const VoxelTerrainHeightFieldParameters& params,
    const float3& positionWorld);
float3 EvaluateVoxelTerrainHeightFieldGradient(
    const VoxelTerrainHeightFieldParameters& params,
    const float3& positionWorld);
bool EvaluateVoxelTerrainHeightFieldNormal(
    const VoxelTerrainHeightFieldParameters& params,
    const float3& positionWorld,
    float3& outNormalWorld);

VoxelTerrainChunkDesc CreateVoxelTerrainChunkDesc(
    const VoxelTerrainComponent& terrain,
    const float3& chunkOriginWorld);
VoxelTerrainDebugValidationDesc CreateVoxelTerrainDebugValidationDesc(
    const VoxelTerrainComponent& terrain,
    const VoxelTerrainChunkDesc& chunkDesc);

} // namespace baamboo