#pragma once
#include <array>

#include "RenderCommon/RenderNode.h"
#include "BaambooScene/VoxelTerrain/VoxelTerrainTypes.h"

struct VoxelTerrainRenderView;

namespace baamboo
{


class VoxelChunkRenderNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	static constexpr u32 kMaxChunks        = 1024u;
	static constexpr u32 kMaxResidentSlabs = 1u;           // single chunk today; multi-chunk streaming grows this
	// Per-slab capacities sized for a worst-case 256^3 chunk.
	static constexpr u32 kVertexSlabCapacity   = 4718592u; // verts / chunk (= 1572864 tris, ~200MB verts)
	static constexpr u32 kMeshletVertexSlabCap = 6291456u; // meshlet-local vertex indices
	static constexpr u32 kMeshletTriSlabCap    = 2097152u; // packed triangle words
	static constexpr u32 kMeshletSlabCapacity  = 131072u;  // meshlets / chunk (ceil(1572864/21)=74899)

	static constexpr u32 kDensityApron     = kVoxelDensityApron;
	static constexpr u32 kDensityVolumeDim = kDefaultVoxelSamplesPerAxis + 2u * kVoxelDensityApron; // 257 + 4 = 261

	static constexpr u32 kTrianglesPerMeshlet  = 21u; // 21 tris * 3 = 63 verts fits the 64-vertex mesh-shader limit
	static constexpr u32 kMaxTrianglesPerChunk = kVertexSlabCapacity / 3u;

	// Morton-coded 32^3 sort blocks over the chunk cube (a multiple of the 1024-thread scan pass).
	static constexpr u32 kTriSortBins = 32u * 32u * 32u;

	// Erosion detail map dimension (XZ heightfield).
	static constexpr u32 kErosionMapDim = 2048u;

	VoxelChunkRenderNode(render::RenderDevice& rd);
	virtual ~VoxelChunkRenderNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

	// GPU geometry build (density -> marching cubes -> vertex/meshlet pools + chunk counts); driven by CullingNode.
	void BuildChunkGeometryIfNeeded(render::CommandContext& context, const SceneRenderView& renderView);

private:
	bool EnsureChunkResident(render::CommandContext& context, const VoxelTerrainRenderView& terrainView); // slab + static index arrays + chunk row
	u32  AllocateSlab();

	void DispatchDensity(render::CommandContext& context, const VoxelTerrainRenderView& terrainView);
	void DispatchExtraction(render::CommandContext& context, const VoxelTerrainRenderView& terrainView);
	void DispatchErosionBake(render::CommandContext& context, const VoxelTerrainRenderView& terrainView);

private:
	// Persistent geometry pools (device-local)
	Arc< render::Buffer > m_pVertexPool;
	Arc< render::Buffer > m_pMeshletPool;
	Arc< render::Buffer > m_pMeshletVertexPool;
	Arc< render::Buffer > m_pMeshletTrianglePool;

	Arc< render::Buffer > m_pChunkCountsBuffer;

	std::vector< u32 > m_FreeSlabs;
	u32                m_SlabHighWater = 0u;

	u32  m_ChunkSlabId   = kInvalidIndex; // reused across rebuilds (single chunk)
	u32  m_BuiltRevision = kInvalidIndex; // last built revision (sentinel => first frame builds)

	Box< render::ComputePipeline > m_pMCExtractPSO;
	Box< render::ComputePipeline > m_pMeshletBuildPSO;

	Arc< render::Texture >          m_pDensityVolume; // written by VoxelDensityCS; MC reads the linear copy below
	Arc< render::Buffer >           m_pDensityField;  // linear density copy the MC extract samples
	Box< render::ComputePipeline >  m_pDensityPSO;

	// Erosion detail map: RGBA16F = R detail height (m), G ridgeMap, B surfaceY, A unused
	Arc< render::Texture >         m_pErosionDetailMap;
	Box< render::ComputePipeline > m_pErosionBakePSO;

	Arc< render::Buffer > m_pMCTriTable; // 256x16 MC triangle-edge table (SSBO, uploaded once)
	Arc< render::Buffer > m_pMCCounter;  // [triangleCount, activeCellCount]

	// Triangle spatial sort: MC append order -> Morton-block order, baked into the meshlet-vertex indirection
	Arc< render::Buffer >          m_pTriSortBins; // kTriSortBins histogram / scanned offsets / scatter cursors
	Box< render::ComputePipeline > m_pTriSortCountPSO;
	Box< render::ComputePipeline > m_pTriSortScanPSO;
	Box< render::ComputePipeline > m_pTriSortScatterPSO;

	bool m_bTriTableUploaded = false;
	// Per-corner identity/pattern index arrays are slab-local
	std::array< bool, kMaxResidentSlabs > m_SlabStaticUploaded = {};
};


} // namespace baamboo
