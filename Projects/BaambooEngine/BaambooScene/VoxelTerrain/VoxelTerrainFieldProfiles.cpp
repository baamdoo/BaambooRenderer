#include "BaambooPch.h"
#include "VoxelTerrainFieldProfiles.h"

#include "../Components.h"
#include "SDFPrimitives.h"

#include <array>
#include <cmath>
#include <glm/gtx/euler_angles.hpp>

namespace baamboo
{

namespace
{

constexpr float kPi = 3.14159265358979323846f;

constexpr VoxelTerrainHeightFieldParameters kFlatHeightField = {
    VoxelTerrainHeightFieldShape::Constant,
    31.0f,
    0.0f,
    0.0f,
    32.0f,
    32.0f,
    0.0f,
    48.0f,
    64.0f,
};

constexpr VoxelTerrainHeightFieldParameters kSlopedHeightField = {
    VoxelTerrainHeightFieldShape::Plane,
    31.0f,
    0.18f,
    -0.11f,
    32.0f,
    32.0f,
    0.0f,
    48.0f,
    64.0f,
};

constexpr VoxelTerrainHeightFieldParameters kPeriodicHeightField = {
    VoxelTerrainHeightFieldShape::Periodic,
    31.0f,
    0.0f,
    0.0f,
    32.0f,
    32.0f,
    6.0f,
    48.0f,
    64.0f,
};

struct VoxelTerrainFieldProfileRecord
{
    VoxelTerrainFieldProfile profile;
    const VoxelTerrainHeightFieldParameters* pHeightField = nullptr;
};

constexpr std::array< VoxelTerrainFieldProfileRecord, 8 > kFieldProfiles = {{
    { { VoxelTerrainFieldPreset::SphereRegression, "Sphere Regression" }, nullptr },
    { { VoxelTerrainFieldPreset::AxisAlignedBox, "Axis-Aligned Box" }, nullptr },
    { { VoxelTerrainFieldPreset::Capsule, "Capsule" }, nullptr },
    { { VoxelTerrainFieldPreset::UniformTransformedBox, "Uniform Transformed Box" }, nullptr },
    { { VoxelTerrainFieldPreset::NonUniformDistanceLikeBox, "Non-Uniform DistanceLike Box" }, nullptr },
    { { VoxelTerrainFieldPreset::HeightFieldFlat, "Height Field - Flat" }, &kFlatHeightField },
    { { VoxelTerrainFieldPreset::HeightFieldSloped, "Height Field - Sloped" }, &kSlopedHeightField },
    { { VoxelTerrainFieldPreset::HeightFieldPeriodic, "Height Field - Periodic" }, &kPeriodicHeightField },
}};

const VoxelTerrainFieldProfileRecord& FindFieldProfileRecord(VoxelTerrainFieldPreset preset)
{
    for (const VoxelTerrainFieldProfileRecord& record : kFieldProfiles)
    {
        if (record.profile.preset == preset)
            return record;
    }

    return kFieldProfiles.front();
}

quat MakeEulerYXZRotation(const float3& eulerDegrees)
{
    const float3 radians = glm::radians(eulerDegrees);
    const mat4 rotation = glm::eulerAngleYXZ(radians.y, radians.x, radians.z);
    return glm::normalize(glm::quat_cast(rotation));
}

float EvaluateUniformTransformedBoxSDF(
    const float3& p,
    const float3& center,
    const quat& rotationToParent,
    float uniformScale,
    const float3& halfExtent)
{
    const float3 primitiveP = SDF::InverseTransformPointUniformScale(
        p,
        center,
        rotationToParent,
        uniformScale);
    return SDF::ApplyUniformScaleToDistance(
        SDF::AxisAlignedBox(primitiveP, float3(0.0f), halfExtent),
        uniformScale);
}

float EvaluateNonUniformDistanceLikeBoxField(
    const float3& p,
    const float3& center,
    const quat& rotationToParent,
    const float3& nonUniformScale,
    const float3& halfExtent)
{
    const float3 primitiveP = SDF::InverseTransformPointNonUniformScale(
        p,
        center,
        rotationToParent,
        nonUniformScale);
    return SDF::AxisAlignedBox(primitiveP, float3(0.0f), halfExtent);
}

bool IsFinite(const float3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

u32 GetVoxelTerrainFieldPresetCount()
{
    return static_cast< u32 >(kFieldProfiles.size());
}

VoxelTerrainFieldPreset GetVoxelTerrainFieldPresetAt(u32 index)
{
    if (index >= GetVoxelTerrainFieldPresetCount())
        return kFieldProfiles.front().profile.preset;

    return kFieldProfiles[index].profile.preset;
}

bool TryGetVoxelTerrainFieldPresetIndex(VoxelTerrainFieldPreset preset, i32& outIndex)
{
    for (u32 index = 0u; index < GetVoxelTerrainFieldPresetCount(); ++index)
    {
        if (kFieldProfiles[index].profile.preset == preset)
        {
            outIndex = static_cast< i32 >(index);
            return true;
        }
    }

    outIndex = 0;
    return false;
}

const VoxelTerrainFieldProfile& GetVoxelTerrainFieldProfile(VoxelTerrainFieldPreset preset)
{
    return FindFieldProfileRecord(preset).profile;
}

const char* GetVoxelTerrainFieldPresetName(VoxelTerrainFieldPreset preset)
{
    return GetVoxelTerrainFieldProfile(preset).displayName;
}

const VoxelTerrainHeightFieldParameters* GetVoxelTerrainHeightFieldParameters(VoxelTerrainFieldPreset preset)
{
    return FindFieldProfileRecord(preset).pHeightField;
}

const char* GetVoxelTerrainHeightFieldShapeName(VoxelTerrainHeightFieldShape shape)
{
    switch (shape)
    {
    case VoxelTerrainHeightFieldShape::Constant:
        return "Constant";
    case VoxelTerrainHeightFieldShape::Plane:
        return "Plane";
    case VoxelTerrainHeightFieldShape::Periodic:
        return "Periodic";
    }

    return "Unknown";
}

float EvaluateVoxelTerrainHeightFieldHeight(
    const VoxelTerrainHeightFieldParameters& params,
    float xWorld,
    float zWorld)
{
    switch (params.shape)
    {
    case VoxelTerrainHeightFieldShape::Constant:
        return params.baseHeightMeter;
    case VoxelTerrainHeightFieldShape::Plane:
        return params.baseHeightMeter +
            params.slopeX * (xWorld - params.anchorXMeter) +
            params.slopeZ * (zWorld - params.anchorZMeter);
    case VoxelTerrainHeightFieldShape::Periodic:
    {
        const float phaseX = 2.0f * kPi * (xWorld - params.anchorXMeter) / params.wavelengthXMeter;
        const float phaseZ = 2.0f * kPi * (zWorld - params.anchorZMeter) / params.wavelengthZMeter;
        return params.baseHeightMeter + params.amplitudeMeter * std::sin(phaseX) * std::cos(phaseZ);
    }
    }

    return params.baseHeightMeter;
}

float EvaluateVoxelTerrainHeightFieldSignedField(
    const VoxelTerrainHeightFieldParameters& params,
    const float3& positionWorld)
{
    return positionWorld.y - EvaluateVoxelTerrainHeightFieldHeight(params, positionWorld.x, positionWorld.z);
}

float3 EvaluateVoxelTerrainHeightFieldGradient(
    const VoxelTerrainHeightFieldParameters& params,
    const float3& positionWorld)
{
    switch (params.shape)
    {
    case VoxelTerrainHeightFieldShape::Constant:
        return float3(0.0f, 1.0f, 0.0f);
    case VoxelTerrainHeightFieldShape::Plane:
        return float3(-params.slopeX, 1.0f, -params.slopeZ);
    case VoxelTerrainHeightFieldShape::Periodic:
    {
        const float phaseX = 2.0f * kPi * (positionWorld.x - params.anchorXMeter) / params.wavelengthXMeter;
        const float phaseZ = 2.0f * kPi * (positionWorld.z - params.anchorZMeter) / params.wavelengthZMeter;
        const float dhdx = params.amplitudeMeter * (2.0f * kPi / params.wavelengthXMeter) * std::cos(phaseX) * std::cos(phaseZ);
        const float dhdz = -params.amplitudeMeter * (2.0f * kPi / params.wavelengthZMeter) * std::sin(phaseX) * std::sin(phaseZ);
        return float3(-dhdx, 1.0f, -dhdz);
    }
    }

    return float3(0.0f, 1.0f, 0.0f);
}

bool EvaluateVoxelTerrainHeightFieldNormal(
    const VoxelTerrainHeightFieldParameters& params,
    const float3& positionWorld,
    float3& outNormalWorld)
{
    const float3 gradient = EvaluateVoxelTerrainHeightFieldGradient(params, positionWorld);
    const float gradientLength = glm::length(gradient);
    if (!IsFinite(gradient) || !std::isfinite(gradientLength) || gradientLength <= 1.0e-6f)
        return false;

    outNormalWorld = gradient / gradientLength;
    return true;
}

VoxelTerrainChunkDesc CreateVoxelTerrainChunkDesc(
    const VoxelTerrainComponent& terrain,
    const float3& chunkOriginWorld)
{
    VoxelTerrainChunkDesc desc{};
    desc.originWorld = chunkOriginWorld;
    desc.settings = terrain.settings;

    switch (terrain.fieldPreset)
    {
    case VoxelTerrainFieldPreset::SphereRegression:
    {
        const float3 center = chunkOriginWorld + float3(terrain.settings.chunkWorldSizeMeter * 0.5f);
        const float radius = terrain.settings.chunkWorldSizeMeter * 0.375f;
        desc.SDF = [center, radius](const float3& p)
            {
                return SDF::Sphere(p, center, radius);
            };
        break;
    }
    case VoxelTerrainFieldPreset::AxisAlignedBox:
        desc.SDF = [center = chunkOriginWorld + terrain.boxCenter,
            halfExtent = terrain.boxHalfExtent](const float3& p)
            {
                return SDF::AxisAlignedBox(p, center, halfExtent);
            };
        break;
    case VoxelTerrainFieldPreset::Capsule:
        desc.SDF = [segmentA = chunkOriginWorld + terrain.capsuleSegmentA,
            segmentB = chunkOriginWorld + terrain.capsuleSegmentB,
            radius = terrain.capsuleRadius](const float3& p)
            {
                return SDF::Capsule(p, segmentA, segmentB, radius);
            };
        break;
    case VoxelTerrainFieldPreset::UniformTransformedBox:
    {
        const quat rotation = MakeEulerYXZRotation(terrain.transformBoxEulerDegrees);
        desc.SDF = [center = chunkOriginWorld + terrain.transformBoxCenter,
            rotation,
            uniformScale = terrain.transformUniformScale,
            halfExtent = terrain.transformBoxHalfExtent](const float3& p)
            {
                return EvaluateUniformTransformedBoxSDF(p, center, rotation, uniformScale, halfExtent);
            };
        break;
    }
    case VoxelTerrainFieldPreset::NonUniformDistanceLikeBox:
    {
        const quat rotation = MakeEulerYXZRotation(terrain.transformBoxEulerDegrees);
        desc.SDF = [center = chunkOriginWorld + terrain.transformBoxCenter,
            rotation,
            nonUniformScale = terrain.transformNonUniformScale,
            halfExtent = terrain.transformBoxHalfExtent](const float3& p)
            {
                return EvaluateNonUniformDistanceLikeBoxField(p, center, rotation, nonUniformScale, halfExtent);
            };
        break;
    }
    case VoxelTerrainFieldPreset::HeightFieldFlat:
    case VoxelTerrainFieldPreset::HeightFieldSloped:
    case VoxelTerrainFieldPreset::HeightFieldPeriodic:
    {
        const VoxelTerrainHeightFieldParameters* params = GetVoxelTerrainHeightFieldParameters(terrain.fieldPreset);
        if (params)
        {
            desc.SDF = [heightField = *params](const float3& p)
                {
                    return EvaluateVoxelTerrainHeightFieldSignedField(heightField, p);
                };
        }
        break;
    }
    }

    return desc;
}

} // namespace baamboo