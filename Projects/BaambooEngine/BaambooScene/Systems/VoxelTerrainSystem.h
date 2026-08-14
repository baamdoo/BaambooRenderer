#pragma once
#include "SceneSystem.h"
#include "BaambooScene/VoxelTerrain/VoxelTerrainTypes.h"

namespace baamboo
{

class VoxelTerrainSystem : public SceneSystem< VoxelTerrainComponent >
{
using Super = SceneSystem< VoxelTerrainComponent >;
public:
    explicit VoxelTerrainSystem(entt::registry& registry) : Super(registry) {}

    virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;

    virtual std::vector< u64 > UpdateRenderData(const EditorCamera& edCamera) override;
    virtual void CollectRenderData(SceneRenderView& outView) const override;

private:
    u32 m_MeshRevision = 0u;

    struct DesiredChunk
    {
        VoxelChunkID id;
        u32 mask;
	};
	std::vector< DesiredChunk > m_DesiredChunks;
	// Per-level snap pair-cell (x, z), kept across frames for the 0.5*S_k deadband
	std::vector< int2 >         m_SnapCells;
	u32                         m_NumRecenters = 0u;
};

} // namespace baamboo
