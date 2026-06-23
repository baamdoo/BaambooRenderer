#include "BaambooPch.h"
#include "VoxelTerrainSystem.h"

#include "TransformSystem.h"
#include "../VoxelTerrain/VoxelTerrainFieldProfiles.h"


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

float3 GetChunkLocalPosition(const VoxelTerrainChunkComponent& chunk, const VoxelTerrainSettings& settings)
{
    return float3(
        static_cast< float >(chunk.coord.x),
        static_cast< float >(chunk.coord.y),
        static_cast< float >(chunk.coord.z)) * settings.chunkWorldSizeMeter;
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

    m_Registry.view< VoxelTerrainComponent >().each([this](auto entity, auto&)
        {
            NormalizeRootTransform(entity);
        });

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
            if (terrain.bDirtyMark)
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

bool VoxelTerrainSystem::NormalizeRootTransform(entt::entity rootEntity)
{
    if (!m_Registry.valid(rootEntity) ||
        !m_Registry.all_of< TransformComponent, VoxelTerrainComponent >(rootEntity))
    {
        return false;
    }

    const auto& terrain = m_Registry.get< VoxelTerrainComponent >(rootEntity);
    auto& transform = m_Registry.get< TransformComponent >(rootEntity);

    const float3 expectedPosition = terrain.terrainOriginWorld;
    const float3 expectedRotation = float3(0.0f);
    const float3 expectedScale = float3(1.0f);
    if (NearlyEqual3(transform.transform.position, expectedPosition) &&
        NearlyEqual3(transform.transform.rotation, expectedRotation) &&
        NearlyEqual3(transform.transform.scale, expectedScale))
    {
        transform.transform.Update();
        return false;
    }

    transform.transform.position = expectedPosition;
    transform.transform.rotation = expectedRotation;
    transform.transform.scale = expectedScale;
    transform.transform.Update();
    m_Registry.patch< TransformComponent >(rootEntity, [](auto&) {});
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

    const float3 expectedPosition = GetChunkLocalPosition(chunk, terrain.settings);
    const float3 expectedRotation = float3(0.0f);
    const float3 expectedScale = float3(1.0f);
    if (NearlyEqual3(transform.transform.position, expectedPosition) &&
        NearlyEqual3(transform.transform.rotation, expectedRotation) &&
        NearlyEqual3(transform.transform.scale, expectedScale))
    {
        transform.transform.Update();
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
    {
        NormalizeRootTransform(chunk.root);
        NormalizeChunkTransform(chunkEntity);
    }

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

float3 VoxelTerrainSystem::GetChunkOriginWorld(const VoxelTerrainChunkComponent& chunk, const VoxelTerrainComponent& terrain)
{
    return terrain.terrainOriginWorld + GetChunkLocalPosition(chunk, terrain.settings);
}

bool VoxelTerrainSystem::RebuildRoot(entt::entity rootEntity)
{
    if (!m_Registry.valid(rootEntity) || !m_Registry.all_of< VoxelTerrainComponent >(rootEntity))
        return false;

    auto& terrain = m_Registry.get< VoxelTerrainComponent >(rootEntity);
    const u64 rootId = entt::to_integral(rootEntity);
    NormalizeRootTransform(rootEntity);

    auto FailRebuild = [&terrain]()
        {
            terrain.bDirtyMark = false;
            return false;
        };

    ProceduralTerrain candidateTerrain;
    candidateTerrain.Initialize(terrain.settings);

    struct CandidateChunkIndex
    {
        entt::entity entity = entt::null;
        u32 chunkIndex = kInvalidIndex;
    };

    std::vector< CandidateChunkIndex > candidateChunkIndices;
    bool bFailed = false;

    m_Registry.view< VoxelTerrainChunkComponent >().each([this, rootEntity, &candidateTerrain, &candidateChunkIndices, &bFailed](auto chunkEntity, auto& chunk)
        {
            if (bFailed || chunk.root != rootEntity)
                return;

            NormalizeChunkTransform(chunkEntity);

            u32 candidateChunkIndex = kInvalidIndex;
            if (!RebuildChunkCandidate(rootEntity, chunkEntity, candidateTerrain, candidateChunkIndex))
            {
                bFailed = true;
                return;
            }

            candidateChunkIndices.push_back({ chunkEntity, candidateChunkIndex });
        });

    if (bFailed)
        return FailRebuild();

    m_Terrains[rootId] = std::move(candidateTerrain);

    for (const CandidateChunkIndex& candidateIndex : candidateChunkIndices)
    {
        if (!m_Registry.valid(candidateIndex.entity) || !m_Registry.all_of< VoxelTerrainChunkComponent >(candidateIndex.entity))
            continue;

        auto& chunk = m_Registry.get< VoxelTerrainChunkComponent >(candidateIndex.entity);
        if (chunk.root == rootEntity)
            chunk.chunkIndex = candidateIndex.chunkIndex;
    }

    terrain.builtFieldPreset = terrain.fieldPreset;
    terrain.bDirtyMark = false;
    if (!candidateChunkIndices.empty())
        ++terrain.meshRevision;

    m_Registry.view< VoxelTerrainChunkComponent >().each([this, rootEntity](auto chunkEntity, auto& chunk)
        {
            if (chunk.root == rootEntity)
                RefreshMeshComponent(chunkEntity);
        });

    return true;
}

bool VoxelTerrainSystem::RebuildChunkCandidate(
    entt::entity rootEntity,
    entt::entity chunkEntity,
    ProceduralTerrain& terrainData,
    u32& outChunkIndex)
{
    auto& terrain = m_Registry.get< VoxelTerrainComponent >(rootEntity);
    auto& chunk = m_Registry.get< VoxelTerrainChunkComponent >(chunkEntity);
    const float3 chunkOriginWorld = GetChunkOriginWorld(chunk, terrain);

    const VoxelTerrainChunkDesc desc = CreateVoxelTerrainChunkDesc(terrain, chunkOriginWorld);
    outChunkIndex = terrainData.CreateChunk(desc);

    if (outChunkIndex == kInvalidIndex)
        return false;

    if (!terrainData.BuildChunkSamples(outChunkIndex))
        return false;

    if (!terrainData.BuildChunkMesh(outChunkIndex))
        return false;

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