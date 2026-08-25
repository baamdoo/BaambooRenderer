#include "BaambooPch.h"
#include "VoxelTerrainSystem.h"

#include "BaambooScene/Camera.h"
#include "BaambooScene/VoxelTerrain/VoxelTerrainTypes.h"

#include <algorithm>


namespace baamboo
{

void VoxelTerrainSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    ++m_MeshRevision; // voxel node rebuilds its chunk only when this changes
    Super::OnComponentUpdated(registry, entity);
}

std::vector< u64 > VoxelTerrainSystem::UpdateRenderData(const float3& cameraPos)
{
    m_ExpiredEntities.clear();
    m_DesiredChunks.clear();

    auto view = m_Registry.view< VoxelTerrainComponent >();
    for (auto entity : view)
    {
        const auto& terrain = view.get< VoxelTerrainComponent >(entity);
        const VoxelTerrainSettings& s = terrain.settings;

        const u32 numLevels = std::min(s.maxLodLevel, 7u) + 1u;
        // Vertical range the surface can reach: floor plane up to base + relief + erosion + dice headroom
        const float maxSurfaceY = s.surfaceBaseYMeter + 0.5f * s.mountainAmplitude + s.erosionStrength * s.erosionScale + 0.5f + 2.0f;

        u32 expectedCount = 0u;
        if (m_SnapCells.size() != numLevels)
            m_SnapCells.assign(numLevels, int2(0));

        struct LevelSpan { i32 xBegin, xEnd, zBegin, zEnd; };
        std::array< LevelSpan, 8u > spans = {};

        for (u32 lod = 0u; lod < numLevels; ++lod)
        {
            const float S        = s.chunkWorldSizeMeter * float(1u << lod);
            const float pairSize = 2.0f * S;

            // deadband: keep the stored pair cell until the camera leaves it by more than 0.5*S
            int2& c = m_SnapCells[lod];
            const int2 prev = c;
            if (cameraPos.x < float(c.x) * pairSize - 0.5f * S || cameraPos.x >= float(c.x + 1) * pairSize + 0.5f * S)
                c.x = (i32)std::floor(cameraPos.x / pairSize);
            if (cameraPos.z < float(c.y) * pairSize - 0.5f * S || cameraPos.z >= float(c.y + 1) * pairSize + 0.5f * S)
                c.y = (i32)std::floor(cameraPos.z / pairSize);
            if (c.x != prev.x || c.y != prev.y)
                ++m_NumRecenters;

            LevelSpan& span = spans[lod];
            span = { 2 * c.x - 2, 2 * c.x + 4, 2 * c.y - 2, 2 * c.y + 4 };

            i32 hxB = 0, hxE = 0, hzB = 0, hzE = 0;
            if (lod > 0u)
            {
                const LevelSpan& fine = spans[lod - 1u];
                hxB = fine.xBegin / 2; hxE = fine.xEnd / 2;
                hzB = fine.zBegin / 2; hzE = fine.zEnd / 2;
                while (hxB - 1 < span.xBegin) { span.xBegin -= 2; span.xEnd -= 2; }
                while (hxE + 1 > span.xEnd)   { span.xBegin += 2; span.xEnd += 2; }
                while (hzB - 1 < span.zBegin) { span.zBegin -= 2; span.zEnd -= 2; }
                while (hzE + 1 > span.zEnd)   { span.zBegin += 2; span.zEnd += 2; }
            }

            // the floor plane is the hard lower bound (below it is air)
            const i32 yBegin = (i32)std::floor(kVoxelWorldFloorYMeter / S);
            const i32 yEnd   = (i32)std::floor(maxSurfaceY / S);
            BB_ASSERT(lod > 0u || (yEnd - yBegin + 1) <= 3, "LOD0 y-layer count %d exceeds the design budget of 3", yEnd - yBegin + 1);
            expectedCount += u32(yEnd - yBegin + 1) * (lod == 0u ? 36u : 27u);

            for (i32 x = span.xBegin; x < span.xEnd; ++x)
            {
                for (i32 z = span.zBegin; z < span.zEnd; ++z)
                {
                    if (lod > 0u && x >= hxB && x < hxE && z >= hzB && z < hzE)
                        continue;

                    u32 mask = 0u;
                    if (lod + 1u < numLevels)
                    {
                        if (x == span.xBegin)   mask |= 1u << 0u; // -x
                        if (x == span.xEnd - 1) mask |= 1u << 1u; // +x
                        if (z == span.zBegin)   mask |= 1u << 4u; // -z
                        if (z == span.zEnd - 1) mask |= 1u << 5u; // +z
                    }
                    for (i32 y = yBegin; y <= yEnd; ++y)
                        m_DesiredChunks.push_back({ VoxelChunkID{ int3(x, y, z), lod }, mask });
                }
            }
        }

        BB_ASSERT(m_DesiredChunks.size() == expectedCount, "ring cut count mismatch: %u != %u", (u32)m_DesiredChunks.size(), expectedCount);

        break;
    }

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
        vt.bValid                   = true;
        vt.revision                 = m_MeshRevision;
        vt.recenterCount            = m_NumRecenters;
        vt.chunkWorldSizeMeter      = s.chunkWorldSizeMeter;

        vt.dice          = s.dice;
        vt.dice.maxLevel = s.dice.maxLevel > 5u ? 5u : s.dice.maxLevel;

        VoxelTerrainGenParams& gp = vt.genParams;
        gp = {};
        gp.voxelSizeMeter = s.voxelSizeMeter;
        gp.cellsPerAxis   = s.cellsPerAxis;
        gp.samplesPerAxis = s.samplesPerAxis;
        gp.apron          = kVoxelDensityApron;

        // procedural surface (Layer 0)
        gp.seed                  = s.seed;
        gp.frequency             = s.frequency;
        gp.octaves               = s.octaves;
        gp.lacunarity            = s.lacunarity;
        gp.gain                  = s.gain;
        gp.warpStrength          = s.warpStrength;
        gp.warpFrequency         = s.warpFrequency;
        gp.mountainAmplitude     = s.mountainAmplitude;
        gp.detailWeight          = s.detailWeight;
        gp.redistributionExp     = s.redistributionExp;
        gp.ridgedBlend           = s.ridgedBlend;
        gp.surfaceBaseYMeter     = s.surfaceBaseYMeter;
        gp.geoMinWavelengthMeter = 2.0f * s.voxelSizeMeter;

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

        for (const DesiredChunk& desired : m_DesiredChunks)
        {
            vt.chunks.emplace_back();

            VoxelChunkView& chunk = vt.chunks.back();
            chunk.id       = desired.id;
            chunk.originWS = float3(desired.id.coord * (i32)s.cellsPerAxis) * (s.voxelSizeMeter * float(1u << desired.id.lod));
            chunk.mask     = desired.mask;
		}

		break; // terrain system only supports a single entity with terrain component
    }
}

} // namespace baamboo
