#pragma once
#include "SceneSystem.h"

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
};

} // namespace baamboo
