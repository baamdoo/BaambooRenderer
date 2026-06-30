#include "BaambooPch.h"
#include "VoxelTerrainSystem.h"

#include "../VoxelTerrain/VoxelTerrainTypes.h"

namespace baamboo
{

void VoxelTerrainSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    ++m_MeshRevision; // voxel node rebuilds its chunk only when this changes
    Super::OnComponentUpdated(registry, entity);
}

std::vector< u64 > VoxelTerrainSystem::UpdateRenderData(const EditorCamera& edCamera)
{
    UNUSED(edCamera);

    std::vector< u64 > markedEntities;
    markedEntities.reserve(m_DirtyEntities.size());
    for (auto entity : m_DirtyEntities)
        markedEntities.emplace_back(entt::to_integral(entity));

    ClearDirtyEntities();
    return markedEntities;
}

void VoxelTerrainSystem::CollectRenderData(SceneRenderView& outView) const
{
    auto view = m_Registry.view< VoxelTerrainComponent >();
    for (auto entity : view)
    {
        const auto& terrain = view.get< VoxelTerrainComponent >(entity);
        const VoxelTerrainSettings& s = terrain.settings;

        VoxelTerrainRenderView& vt = outView.voxelTerrain;
        vt.bValid              = true;
        vt.revision            = m_MeshRevision;
        vt.terrainInstance     = entt::to_integral(entity);
        vt.chunkCoord          = int3(0);
        vt.lod                 = 0u;
        vt.fieldRevision       = m_MeshRevision;
        vt.extractionRevision  = kExtractionRevision;
        vt.originWorld         = terrain.terrainOriginWorld;
        vt.chunkWorldSizeMeter = s.chunkWorldSizeMeter;
        vt.voxelSizeMeter      = s.voxelSizeMeter;
        vt.cellsPerAxis        = s.cellsPerAxis;
        vt.samplesPerAxis      = s.samplesPerAxis;

        VoxelTerrainGenParams& gp = vt.genParams;
        gp = {};
        gp.chunkOriginWS  = terrain.terrainOriginWorld;
        gp.voxelSizeMeter = s.voxelSizeMeter;
        gp.cellsPerAxis   = s.cellsPerAxis;
        gp.samplesPerAxis = s.samplesPerAxis;
        gp.apron          = kVoxelDensityApron;
        break; // single chunk; TODO:multi-chunk streaming
    }
}

} // namespace baamboo
