#pragma once
#include "RenderCommon/RenderNode.h"
#include "BaambooScene/Terrain/TerrainQuadtree.h"
#include "BaambooScene/Terrain/TerrainPatch.h"

namespace baamboo
{


class TerrainNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	static constexpr u32 MAX_PATCHES_PER_FRAME = 4096u;

	struct QuadtreeConfig
	{
		std::string heightmapPath;

		u32 gridN = 9u;

		float rootSizeMeter     = 8192.0f;
		float lodRangeBaseMeter = 200.0f;

		float lodMorphK = 0.85f;
		u32   maxDepth  = 5u;

		float terrainSizeMeter = 8192.0f;
		float heightMinMeter   = 0.0f;
		float heightRangeMeter = 2500.0f;

		float rootOriginX = -4096.0f;
		float rootOriginZ = -4096.0f;
	};

	TerrainNode(render::RenderDevice& rd);
	virtual ~TerrainNode() = default;

	void SetQuadtreeConfig(const QuadtreeConfig& cfg);

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

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
		u32   numPatches;      // emit count this frame
		u32   _pad0;

		float lodRangeStart[TERRAIN_MAX_LOD_DEPTHS]; // r_s[d] = k * r_e[d]
		float lodRangeEnd  [TERRAIN_MAX_LOD_DEPTHS]; // r_e[d] = base * 2^(maxDepth - d)
	};
	static_assert(sizeof(TerrainParams) == 48u + 2u * TERRAIN_MAX_LOD_DEPTHS * sizeof(float),
		"TerrainGlobalsCB must remain 16-byte-aligned and match HLSL TerrainGlobals layout");

	void BuildQuadtree();

	void FillTerrainParams(TerrainParams& outParams, u32 numPatches) const;

	void UploadPatches(const std::vector< PatchInstance >& patches);

private:
	QuadtreeConfig    m_Config;
	bool              m_bConfigDirty = true;
	std::string       m_HeightmapPathCache;
	TerrainQuadtree   m_Quadtree;

	Arc< render::Texture >      m_pHeightmap;
	Arc< render::RenderTarget > m_pRenderTarget;

	Arc< render::Buffer > m_pDispatchArgs;
	Arc< render::Buffer > m_pPatchInstances;

	Box< render::GraphicsPipeline > m_pTerrainPSO;
};

} // namespace baamboo
