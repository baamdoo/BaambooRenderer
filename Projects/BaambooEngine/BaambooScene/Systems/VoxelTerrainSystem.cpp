#include "BaambooPch.h"
#include "VoxelTerrainSystem.h"

#include "TransformSystem.h"
#include "../VoxelTerrain/SDFPrimitives.h"

#include <glm/gtx/euler_angles.hpp>

namespace baamboo
{

namespace
{

constexpr const char* kVoxelTerrainChunkTag = "VoxelTerrainChunk";
constexpr const char* kVoxelTerrainGeneratedPath = "$generated/VoxelTerrainChunk";

std::string MakeVoxelTerrainGeneratedTag(u32 revision)
{
    return std::string(kVoxelTerrainChunkTag) + "_" + std::to_string(revision);
}

std::string MakeVoxelTerrainGeneratedPath(u32 revision)
{
    return std::string(kVoxelTerrainGeneratedPath) + "_" + std::to_string(revision);
}

bool NearlyEqual(float a, float b, float epsilon = 1.0e-4f)
{
    return std::abs(a - b) <= epsilon;
}

bool NearlyEqual3(const float3& a, const float3& b, float epsilon = 1.0e-4f)
{
    return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) && NearlyEqual(a.z, b.z, epsilon);
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

} // namespace

VoxelTerrainSystem::VoxelTerrainSystem(entt::registry& registry, TransformSystem* pTransformSystem)
    : Super(registry)
    , m_pTransformSystem(pTransformSystem)
{
}

void VoxelTerrainSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
    MarkTerrainDirty(entity);
    Super::OnComponentConstructed(registry, entity);
}

void VoxelTerrainSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    MarkTerrainDirty(entity);
    Super::OnComponentUpdated(registry, entity);
}

void VoxelTerrainSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
    UNUSED(registry);
    m_Terrains.erase(entt::to_integral(entity));
    Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > VoxelTerrainSystem::UpdateRenderData(const EditorCamera& edCamera)
{
    UNUSED(edCamera);

    std::vector< u64 > markedEntities;

    for (auto entity : m_ExpiredEntities)
    {
        const u64 id = entt::to_integral(entity);
        m_Terrains.erase(id);
        markedEntities.emplace_back(id);
    }
    m_ExpiredEntities.clear();

    RebuildAllDirty();

    m_Registry.view< VoxelTerrainChunkComponent >().each([this](auto entity, auto&)
        {
            NormalizeChunkTransform(entity);
        });

    ClearDirtyEntities();
    return markedEntities;
}

bool VoxelTerrainSystem::Rebuild(entt::entity rootEntity)
{
    if (!m_Registry.valid(rootEntity) || !m_Registry.all_of< VoxelTerrainComponent >(rootEntity))
        return false;

    MarkTerrainDirty(rootEntity);
    return RebuildRoot(rootEntity);
}

void VoxelTerrainSystem::RebuildAllDirty()
{
    m_Registry.view< VoxelTerrainComponent >().each([this](auto entity, auto& terrain)
        {
            const u64 rootId = entt::to_integral(entity);
            const bool bMissingTerrain = !m_Terrains.contains(rootId);
            if (terrain.bDirtyMark || bMissingTerrain)
                RebuildRoot(entity);
        });
}

bool VoxelTerrainSystem::SetMeshVisible(bool bVisible)
{
    if (m_bMeshVisible == bVisible)
        return false;

    m_bMeshVisible = bVisible;
    RefreshAllMeshComponents();
    return true;
}

bool VoxelTerrainSystem::NormalizeChunkTransform(entt::entity chunkEntity)
{
    if (!m_Registry.valid(chunkEntity) ||
        !m_Registry.all_of< TransformComponent, VoxelTerrainChunkComponent >(chunkEntity))
    {
        return false;
    }

    const auto& chunk = m_Registry.get< VoxelTerrainChunkComponent >(chunkEntity);
    if (!chunk.bTransformLocked)
        return false;

    if (chunk.root == entt::null ||
        !m_Registry.valid(chunk.root) ||
        !m_Registry.all_of< VoxelTerrainComponent >(chunk.root))
    {
        return false;
    }

    const auto& terrain = m_Registry.get< VoxelTerrainComponent >(chunk.root);
    auto& transform = m_Registry.get< TransformComponent >(chunkEntity);

    const float3 expectedPosition = GetChunkPosition(chunk, terrain.settings);
    const float3 expectedRotation = float3(0.0f);
    const float3 expectedScale = float3(1.0f);
    if (NearlyEqual3(transform.transform.position, expectedPosition) &&
        NearlyEqual3(transform.transform.rotation, expectedRotation) &&
        NearlyEqual3(transform.transform.scale, expectedScale))
    {
        return false;
    }

    transform.transform.position = expectedPosition;
    transform.transform.rotation = expectedRotation;
    transform.transform.scale = expectedScale;
    transform.transform.Update();
    m_Registry.patch< TransformComponent >(chunkEntity, [](auto&) {});
    return true;
}

void VoxelTerrainSystem::RefreshMeshComponent(entt::entity chunkEntity)
{
    if (!m_Registry.valid(chunkEntity) ||
        !m_Registry.all_of< StaticMeshComponent, VoxelTerrainChunkComponent >(chunkEntity))
    {
        return;
    }

    const auto& chunk = m_Registry.get< VoxelTerrainChunkComponent >(chunkEntity);
    if (chunk.root == entt::null ||
        !m_Registry.valid(chunk.root) ||
        !m_Registry.all_of< VoxelTerrainComponent >(chunk.root))
    {
        return;
    }

    auto& terrain = m_Registry.get< VoxelTerrainComponent >(chunk.root);
    auto& meshComponent = m_Registry.get< StaticMeshComponent >(chunkEntity);
    ResetMeshComponent(meshComponent, terrain);

    const SDFChunk* sdfChunk = GetChunk(chunkEntity);
    if (sdfChunk)
        NormalizeChunkTransform(chunkEntity);

    if (sdfChunk && m_bMeshVisible)
    {
        const TerrainMeshData& meshData = sdfChunk->MeshData();
        if (!meshData.vertices.empty() && !meshData.indices.empty() && meshData.bHasBounds)
        {
            meshComponent.aabb = meshData.aabb;
            meshComponent.sphere = BoundingSphere(meshData.aabb);
            meshComponent.pVertices = const_cast< Vertex* >(meshData.vertices.data());
            meshComponent.numVertices = meshData.NumVertices();

            meshComponent.lods[0].pIndices = const_cast< Index* >(meshData.indices.data());
            meshComponent.lods[0].numIndices = meshData.NumIndices();
            if (!meshData.meshlets.empty())
            {
                meshComponent.lods[0].pMeshlets = const_cast< Meshlet* >(meshData.meshlets.data());
                meshComponent.lods[0].numMeshlets = meshData.NumMeshlets();
            }
            if (!meshData.meshletVertices.empty())
            {
                meshComponent.lods[0].pMeshletVertices = const_cast< u32* >(meshData.meshletVertices.data());
                meshComponent.lods[0].numMeshletVertices = static_cast< u32 >(meshData.meshletVertices.size());
            }
            if (!meshData.meshletTriangles.empty())
            {
                meshComponent.lods[0].pMeshletTriangles = const_cast< u32* >(meshData.meshletTriangles.data());
                meshComponent.lods[0].numMeshletTriangles = static_cast< u32 >(meshData.meshletTriangles.size());
            }
        }
    }

    m_Registry.patch< StaticMeshComponent >(chunkEntity, [](auto&) {});
}

void VoxelTerrainSystem::RefreshAllMeshComponents()
{
    m_Registry.view< VoxelTerrainChunkComponent >().each([this](auto entity, auto&)
        {
            RefreshMeshComponent(entity);
        });
}

ProceduralTerrain* VoxelTerrainSystem::GetTerrain(entt::entity rootEntity)
{
    const auto it = m_Terrains.find(entt::to_integral(rootEntity));
    return it != m_Terrains.end() ? &it->second : nullptr;
}

const ProceduralTerrain* VoxelTerrainSystem::GetTerrain(entt::entity rootEntity) const
{
    const auto it = m_Terrains.find(entt::to_integral(rootEntity));
    return it != m_Terrains.end() ? &it->second : nullptr;
}

const ProceduralTerrain* VoxelTerrainSystem::GetTerrainForChunk(entt::entity chunkEntity) const
{
    if (!m_Registry.valid(chunkEntity) || !m_Registry.all_of< VoxelTerrainChunkComponent >(chunkEntity))
        return nullptr;

    const auto& chunk = m_Registry.get< VoxelTerrainChunkComponent >(chunkEntity);
    return GetTerrain(chunk.root);
}

const SDFChunk* VoxelTerrainSystem::GetChunk(entt::entity chunkEntity) const
{
    if (!m_Registry.valid(chunkEntity) || !m_Registry.all_of< VoxelTerrainChunkComponent >(chunkEntity))
        return nullptr;

    const auto& chunk = m_Registry.get< VoxelTerrainChunkComponent >(chunkEntity);
    const ProceduralTerrain* terrainData = GetTerrain(chunk.root);
    return terrainData ? terrainData->GetChunk(chunk.chunkIndex) : nullptr;
}

float3 VoxelTerrainSystem::GetChunkOriginWorld(const VoxelTerrainChunkComponent& chunk, const VoxelTerrainSettings& settings)
{
    return float3(
        static_cast< float >(chunk.coord.x),
        static_cast< float >(chunk.coord.y),
        static_cast< float >(chunk.coord.z)) * settings.chunkWorldSizeMeter;
}

float3 VoxelTerrainSystem::GetTerrainPivot(const VoxelTerrainSettings& settings)
{
    return float3(settings.chunkWorldSizeMeter * 0.5f);
}

float3 VoxelTerrainSystem::GetChunkPosition(const VoxelTerrainChunkComponent& chunk, const VoxelTerrainSettings& settings)
{
    return GetChunkOriginWorld(chunk, settings) - GetTerrainPivot(settings);
}

bool VoxelTerrainSystem::RebuildRoot(entt::entity rootEntity)
{
    if (!m_Registry.valid(rootEntity) || !m_Registry.all_of< VoxelTerrainComponent >(rootEntity))
        return false;

    auto& terrain = m_Registry.get< VoxelTerrainComponent >(rootEntity);
    ProceduralTerrain& terrainData = m_Terrains[entt::to_integral(rootEntity)];
    terrainData.Clear();
    terrainData.Initialize(terrain.settings);

    bool bBuiltAnyChunk = false;
    m_Registry.view< VoxelTerrainChunkComponent >().each([this, rootEntity, &terrainData, &bBuiltAnyChunk](auto chunkEntity, auto& chunk)
        {
            if (chunk.root != rootEntity)
                return;

            bBuiltAnyChunk |= RebuildChunk(rootEntity, chunkEntity, terrainData);
        });

    terrain.builtFieldPreset = terrain.fieldPreset;
    terrain.bDirtyMark = false;
    if (bBuiltAnyChunk)
        ++terrain.meshRevision;

    m_Registry.view< VoxelTerrainChunkComponent >().each([this, rootEntity](auto chunkEntity, auto& chunk)
        {
            if (chunk.root == rootEntity)
                RefreshMeshComponent(chunkEntity);
        });
    return bBuiltAnyChunk;
}

bool VoxelTerrainSystem::RebuildChunk(entt::entity rootEntity, entt::entity chunkEntity, ProceduralTerrain& terrainData)
{
    auto& terrain = m_Registry.get< VoxelTerrainComponent >(rootEntity);
    auto& chunk = m_Registry.get< VoxelTerrainChunkComponent >(chunkEntity);
    const float3 chunkOriginWorld = GetChunkOriginWorld(chunk, terrain.settings);

    switch (terrain.fieldPreset)
    {
    case VoxelTerrainFieldPreset::SphereRegression:
        chunk.chunkIndex = terrainData.CreateChunk(chunkOriginWorld);
        break;
    case VoxelTerrainFieldPreset::AxisAlignedBox:
    {
        VoxelTerrainChunkDesc desc{};
        desc.originWorld = chunkOriginWorld;
        desc.settings = terrain.settings;
        desc.SDF = [center = terrain.boxCenter,
            halfExtent = terrain.boxHalfExtent](const float3& p)
            {
                return SDF::AxisAlignedBox(p, center, halfExtent);
            };

        chunk.chunkIndex = terrainData.CreateChunk(desc);
        break;
    }
    case VoxelTerrainFieldPreset::Capsule:
    {
        VoxelTerrainChunkDesc desc{};
        desc.originWorld = chunkOriginWorld;
        desc.settings = terrain.settings;
        desc.SDF = [segmentA = terrain.capsuleSegmentA,
            segmentB = terrain.capsuleSegmentB,
            radius = terrain.capsuleRadius](const float3& p)
            {
                return SDF::Capsule(p, segmentA, segmentB, radius);
            };

        chunk.chunkIndex = terrainData.CreateChunk(desc);
        break;
    }
    case VoxelTerrainFieldPreset::UniformTransformedBox:
    {
        VoxelTerrainChunkDesc desc{};
        desc.originWorld = chunkOriginWorld;
        desc.settings = terrain.settings;
        const quat rotation = MakeEulerYXZRotation(terrain.transformBoxEulerDegrees);
        desc.SDF = [center = terrain.transformBoxCenter,
            rotation,
            uniformScale = terrain.transformUniformScale,
            halfExtent = terrain.transformBoxHalfExtent](const float3& p)
            {
                return EvaluateUniformTransformedBoxSDF(p, center, rotation, uniformScale, halfExtent);
            };

        chunk.chunkIndex = terrainData.CreateChunk(desc);
        break;
    }
    case VoxelTerrainFieldPreset::NonUniformDistanceLikeBox:
    {
        VoxelTerrainChunkDesc desc{};
        desc.originWorld = chunkOriginWorld;
        desc.settings = terrain.settings;
        const quat rotation = MakeEulerYXZRotation(terrain.transformBoxEulerDegrees);
        desc.SDF = [center = terrain.transformBoxCenter,
            rotation,
            nonUniformScale = terrain.transformNonUniformScale,
            halfExtent = terrain.transformBoxHalfExtent](const float3& p)
            {
                return EvaluateNonUniformDistanceLikeBoxField(p, center, rotation, nonUniformScale, halfExtent);
            };

        chunk.chunkIndex = terrainData.CreateChunk(desc);
        break;
    }
    }

    terrainData.BuildChunkSamples(chunk.chunkIndex);
    terrainData.BuildChunkMesh(chunk.chunkIndex);
    NormalizeChunkTransform(chunkEntity);
    return true;
}

void VoxelTerrainSystem::ResetMeshComponent(StaticMeshComponent& meshComponent, const VoxelTerrainComponent& terrain) const
{
    meshComponent.tag = MakeVoxelTerrainGeneratedTag(terrain.meshRevision);
    meshComponent.path = MakeVoxelTerrainGeneratedPath(terrain.meshRevision);
    meshComponent.pVertices = nullptr;
    meshComponent.numVertices = 0u;
    meshComponent.aabb = BoundingBox(float3(0.0f), float3(0.0f));
    meshComponent.sphere = BoundingSphere(float3(0.0f), 0.0f);
    meshComponent.maxLOD = 0u;
    for (auto& lod : meshComponent.lods)
    {
        lod.pIndices = nullptr;
        lod.numIndices = 0u;
        lod.pMeshlets = nullptr;
        lod.numMeshlets = 0u;
        lod.pMeshletVertices = nullptr;
        lod.numMeshletVertices = 0u;
        lod.pMeshletTriangles = nullptr;
        lod.numMeshletTriangles = 0u;
        lod.simplifyError = 0.0f;
    }
}

void VoxelTerrainSystem::MarkTerrainDirty(entt::entity rootEntity)
{
    if (!m_Registry.valid(rootEntity) || !m_Registry.all_of< VoxelTerrainComponent >(rootEntity))
        return;

    auto& terrain = m_Registry.get< VoxelTerrainComponent >(rootEntity);
    terrain.bDirtyMark = true;
}

} // namespace baamboo