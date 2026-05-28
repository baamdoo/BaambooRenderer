#pragma once
#include "RenderCommon/RenderNode.h"
#include "BaambooScene/Terrain/TerrainQuadtree.h"
#include "BaambooScene/Terrain/TerrainPatch.h"

namespace baamboo
{


class GBufferNode;

class TerrainNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	static constexpr u32 MAX_QUADTREE_NODES  = 32768u;  // covers maxDepth=7 (21845) + safety margin
	static constexpr u32 MAX_CULLED_PATCHES  = 32768u;  // Covers 128x128 leaf patches at maxDepth=7 in finest-review mode.

	struct QuadtreeConfig
	{
		std::string heightmapPath;

		u32 gridN = 9u;

		float rootSizeMeter     = 8192.0f;
		float lodRangeBaseMeter = 200.0f;

		float lodMorphK = 0.5f;
		u32   maxDepth  = 5u;
		bool  bForceFinestLOD = true;

		float terrainSizeMeter = 8192.0f;
		float heightMinMeter   = 0.0f;
		float heightRangeMeter = 2500.0f;

		float rootOriginX = -4096.0f;
		float rootOriginZ = -4096.0f;
	};

	TerrainNode(render::RenderDevice& rd);
	virtual ~TerrainNode() = default;

	void SetGBufferNode(const Arc< GBufferNode >& pNode);

	void SetQuadtreeConfig(const QuadtreeConfig& cfg);

	// No-op: TerrainNode is not registered in RenderGraph. CullingNode owns this and drives DrawTerrainPhase1/2.
	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

	void DispatchTerrainCull(render::CommandContext&    context,
	                         u32                        phase,
	                         const Arc< render::Texture >& pHiZTexture,
	                         const SceneRenderView&     renderView);

	void DrawTerrainPhase1(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView);
	void DrawTerrainPhase2(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView);

	const Arc< render::Buffer >& GetDrawCountBuffer() const { return m_pDrawCountBuffer; }
#if PROFILING_LEVEL >= 1
	const Arc< render::Buffer >& GetLodStatsBuffer() const { return m_pLodStatsBuffer; }
#endif
	u32 GetTotalNodeCount() const { return m_Quadtree.NumNodes(); }
	// Per-patch triangle count: surface (2*(N-1)²) + skirt (8*(N-1))
	u32 GetPerPatchTriangles() const
	{
		const u32 P = (m_Config.gridN > 1u) ? (m_Config.gridN - 1u) : 0u;
		return 2u * P * P + 8u * P;
	}

private:
	struct TerrainParams
	{
		float terrainOriginX;
		float terrainOriginZ;
		float terrainSizeMeter;
		u32   gridN;

		float heightMinMeter;
		float heightRangeMeter;
		float heightmapTexel;  // 1 / rho
		float worldPerTexel;   // T / rho

		u32   heightmapRes;    // rho (auto-derived)
		u32   maxDepth;        // 0 = root, maxDepth = leaf
		u32   numPatches;
		u32   _pad0;

		float lodRangeStart[TERRAIN_MAX_LOD_DEPTHS]; // r_s[d] = k * r_e[d]
		float lodRangeEnd  [TERRAIN_MAX_LOD_DEPTHS]; // r_e[d] = base * 2^(maxDepth - d)
	};
	static_assert(sizeof(TerrainParams) == 48u + 2ULL * TERRAIN_MAX_LOD_DEPTHS * sizeof(float),
		"TerrainGlobalsCB must remain 16-byte-aligned and match HLSL TerrainGlobals layout");

	void EnsureHeightmap();
	void EnsureQuadtreeUpload(render::CommandContext& context);
	void BuildQuadtree();
	void FillTerrainParams(TerrainParams& outParams) const;
	void DrawTerrainImpl(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView);

private:
	QuadtreeConfig    m_Config;
	bool              m_bConfigDirty = true;
	std::string       m_HeightmapPathCache;
	TerrainQuadtree   m_Quadtree;

	Arc< GBufferNode > m_pGBufferNode;

	Arc< render::Texture > m_pHeightmap;

	// --- Phase 3 GPU cull resources ---
	Arc< render::Buffer > m_pQuadtreeNodesBuffer;
	Arc< render::Buffer > m_pCulledPatches;
	Arc< render::Buffer > m_pIndirectCommands;
	Arc< render::Buffer > m_pDrawCountBuffer;
	Arc< render::Buffer > m_pLodStatsBuffer;
	Arc< render::Buffer > m_pNodeVisibilityBuffer;

	bool m_bQuadtreeUploaded     = false;
	bool m_bVisibilityNeedsClear = true;

	// --- Pipelines ---
	Box< render::GraphicsPipeline > m_pTerrainPSO;
	Box< render::ComputePipeline >  m_pCullingPSO;
};


} // namespace baamboo
