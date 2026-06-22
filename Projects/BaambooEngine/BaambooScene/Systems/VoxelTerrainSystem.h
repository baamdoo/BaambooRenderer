#pragma once
#include "SceneSystem.h"
#include "../VoxelTerrain/ProceduralTerrain.h"

#include <unordered_map>

namespace baamboo
{

class TransformSystem;

class VoxelTerrainSystem : public SceneSystem< VoxelTerrainComponent >
{
using Super = SceneSystem< VoxelTerrainComponent >;
public:
    VoxelTerrainSystem(entt::registry& registry, TransformSystem* pTransformSystem);

    virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

    virtual std::vector< u64 > UpdateRenderData(const EditorCamera& edCamera) override;

    bool Rebuild(entt::entity rootEntity);
    void RebuildAllDirty();

    bool SetMeshVisible(bool bVisible);
    bool IsMeshVisible() const { return m_bMeshVisible; }

    bool NormalizeRootTransform(entt::entity rootEntity);
    bool NormalizeChunkTransform(entt::entity chunkEntity);
    void RefreshMeshComponent(entt::entity chunkEntity);
    void RefreshAllMeshComponents();

    ProceduralTerrain*       GetTerrain(entt::entity rootEntity);
    const ProceduralTerrain* GetTerrain(entt::entity rootEntity) const;
    const ProceduralTerrain* GetTerrainForChunk(entt::entity chunkEntity) const;
    const SDFChunk*          GetChunk(entt::entity chunkEntity) const;

    static float3 GetChunkOriginWorld(const VoxelTerrainChunkComponent& chunk, const VoxelTerrainComponent& terrain);

private:
    bool RebuildRoot(entt::entity rootEntity);
    bool RebuildChunkCandidate(entt::entity rootEntity, entt::entity chunkEntity, ProceduralTerrain& terrainData, u32& outChunkIndex);
    void ResetMeshComponent(StaticMeshComponent& meshComponent, const VoxelTerrainComponent& terrain) const;
    void MarkTerrainDirty(entt::entity rootEntity);

private:
    TransformSystem* m_pTransformSystem = nullptr;
    std::unordered_map< u64, ProceduralTerrain > m_Terrains;
    bool m_bMeshVisible = true;
};

} // namespace baamboo
