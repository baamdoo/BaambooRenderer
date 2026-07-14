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
        vt.originWorld         = terrain.terrainOriginWorld;
        vt.chunkWorldSizeMeter = s.chunkWorldSizeMeter;

        // Dice/micro params are live-editable: no revision bump, raster/resolve side only.
        vt.dice          = s.dice;
        vt.dice.maxLevel = s.dice.maxLevel > 5u ? 5u : s.dice.maxLevel; // shader ladder tops at L5

        VoxelTerrainGenParams& gp = vt.genParams;
        gp = {};
        gp.chunkOriginWS  = terrain.terrainOriginWorld;
        gp.voxelSizeMeter = s.voxelSizeMeter;
        gp.cellsPerAxis   = s.cellsPerAxis;
        gp.samplesPerAxis = s.samplesPerAxis;
        gp.apron          = kVoxelDensityApron;

        // procedural surface (Layer 0)
        gp.seed              = s.seed;
        gp.frequency         = s.frequency;
        gp.octaves           = s.octaves;
        gp.lacunarity        = s.lacunarity;
        gp.gain              = s.gain;
        gp.warpStrength      = s.warpStrength;
        gp.warpFrequency     = s.warpFrequency;
        gp.mountainAmplitude = s.mountainAmplitude;
        gp.detailWeight      = s.detailWeight;
        gp.redistributionExp = s.redistributionExp;
        gp.ridgedBlend       = s.ridgedBlend;
        gp.surfaceLevelRatio = s.surfaceLevelRatio;

        // Erosion filter (Layer 2)
        gp.erosionScale         = s.erosionScale;
        gp.erosionStrength      = s.erosionStrength;
        gp.erosionGullyWeight   = s.erosionGullyWeight;
        gp.erosionDetail        = s.erosionDetail;
        gp.erosionOnsetInput    = s.erosionOnsetInput;
        gp.erosionOnsetOctave   = s.erosionOnsetOctave;
        gp.erosionCellScale     = s.erosionCellScale;
        gp.erosionNormalization = s.erosionNormalization;
        gp.erosionSlopeScale    = s.erosionSlopeScale;
        gp.erosionOctaves       = s.erosionOctaves;
        break; // single chunk; TODO:multi-chunk streaming
    }
}

} // namespace baamboo
