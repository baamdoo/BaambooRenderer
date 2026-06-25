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

    virtual void CollectRenderData(SceneRenderView& outView) const override;

private:
    static constexpr u32 kExtractionRevision = 0u;

    // Bumped on every authoring edit (OnComponentUpdated), snapshotted into SceneRenderView so the
    // voxel node rebuilds its chunk only when this changes. Kept here rather than on the component
    // so VoxelTerrainComponent stays pure authoring data, like every other component.
    u32 m_MeshRevision = 0u;
};

} // namespace baamboo
