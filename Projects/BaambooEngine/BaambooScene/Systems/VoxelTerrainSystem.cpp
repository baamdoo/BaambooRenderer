#include "BaambooPch.h"
#include "VoxelTerrainSystem.h"

#include "../VoxelTerrain/VoxelTerrainFieldProfiles.h"

namespace baamboo
{

void VoxelTerrainSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    auto& terrain = registry.get< VoxelTerrainComponent >(entity);

    ++m_MeshRevision;
    terrain.builtFieldPreset = terrain.fieldPreset;
    Super::OnComponentUpdated(registry, entity);
}

void VoxelTerrainSystem::CollectRenderData(SceneRenderView& outView) const
{
    auto view = m_Registry.view< VoxelTerrainComponent >();
    for (auto entity : view)
    {
        const auto& terrain = view.get< VoxelTerrainComponent >(entity);

        VoxelTerrainChunkDesc desc = CreateVoxelTerrainChunkDesc(terrain, terrain.terrainOriginWorld);

        VoxelTerrainRenderView& vt = outView.voxelTerrain;
        vt.bValid                  = true;
        vt.revision                = m_MeshRevision;
        vt.terrainInstance         = entt::to_integral(entity);
        vt.chunkCoord              = int3(0);
        vt.lod                     = 0u;
        vt.fieldRevision           = m_MeshRevision;
        vt.extractionRevision      = kExtractionRevision;
        vt.originWorld             = desc.originWorld;
        vt.chunkWorldSizeMeter     = desc.settings.chunkWorldSizeMeter;
        vt.voxelSizeMeter          = desc.settings.voxelSizeMeter;
        vt.cellsPerAxis            = desc.settings.cellsPerAxis;
        vt.samplesPerAxis          = desc.settings.samplesPerAxis;
        vt.normalEpsilonMultiplier = desc.settings.normalEpsilonMultiplier;
        vt.SDF                     = std::move(desc.SDF);
        break;
    }
}

} // namespace baamboo
