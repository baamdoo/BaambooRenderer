#pragma once
#include "RenderCommon/RenderNode.h"
#include "ShaderTypes.h"
#include "BaambooScene/VoxelTerrain/VoxelTerrainTypes.h"

struct VoxelTerrainRenderView;

namespace baamboo
{


class VoxelChunkRenderNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	static constexpr u32 kMaxChunks        = 1024u;
	static constexpr u32 kMaxResidentSlabs = 8u;
	// Per-slab capacities -- sized for a worst-case 64^3 chunk + headroom.
	static constexpr u32 kVertexSlabCapacity   = 294912u; // verts / chunk (= 98304 tris)
	static constexpr u32 kMeshletVertexSlabCap = 393216u; // meshlet-local vertex indices
	static constexpr u32 kMeshletTriSlabCap    = 131072u; // packed triangle words
	static constexpr u32 kMeshletSlabCapacity  = 8192u;   // meshlets / chunk (ceil(98304/21)=4682)

	static constexpr u32 kDensityApron     = kVoxelDensityApron;
	static constexpr u32 kDensityVolumeDim = kDefaultVoxelSamplesPerAxis + 2u * kVoxelDensityApron; // 65 + 4 = 69

	// Per-corner v1: 3 verts/triangle, K triangles/meshlet so 3*K <= VoxelGBufferMS maxvertices (64).
	static constexpr u32 kTrianglesPerMeshlet  = 21u;                      // 3*21 = 63 <= 64
	static constexpr u32 kMaxTrianglesPerChunk = kVertexSlabCapacity / 3u;

	VoxelChunkRenderNode(render::RenderDevice& rd);
	virtual ~VoxelChunkRenderNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

	// --- Driven by CullingNode ---
	void DispatchChunkCull(render::CommandContext& context, u32 phase, const Arc< render::Texture >& pHiZTexture, const SceneRenderView& renderView);
	void DrawChunksPhase1(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView);
	void DrawChunksPhase2(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView);

private:
	void CapturePending(const VoxelTerrainRenderView& terrainView); // pending metadata for the GPU build
	bool EnsureChunkResident(render::CommandContext& context);      // GPU path: slab + static index arrays + row
	u32  AllocateSlab();
	void DiscardPending(u32 rejectedRevision);
	void DrawChunksImpl(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView);
	void PublishPools();

	void DispatchDensity(render::CommandContext& context, const VoxelTerrainRenderView& terrainView);
	void DispatchExtraction(render::CommandContext& context, const VoxelTerrainRenderView& terrainView);

private:
	// --- Persistent geometry pools (device-local; never per-frame reset) ---
	Arc< render::Buffer > m_pVertexPool;
	Arc< render::Buffer > m_pMeshletPool;
	Arc< render::Buffer > m_pMeshletVertexPool;
	Arc< render::Buffer > m_pMeshletTrianglePool;

	// --- Chunk table + GPU-driven draw ---
	Arc< render::Buffer > m_pChunkTableBuffer;
	Arc< render::Buffer > m_pIndirectCommands;
	Arc< render::Buffer > m_pDrawCountBuffer;

	// --- Slab free-list ---
	std::vector< u32 > m_FreeSlabs;
	u32                m_SlabHighWater = 0u;

	// --- Pending build (filled on revision change, consumed by EnsureChunkResident) ---
	float3          m_PendingOriginWS     = float3(0.0f);
	float           m_PendingVoxelSize    = 1.0f;
	float           m_PendingChunkWorldSz = 64.0f;
	u64             m_PendingTerrainInstance    = 0u;
	int3            m_PendingChunkCoord         = int3(0);
	u32             m_PendingLOD                = 0u;
	u32             m_PendingFieldRevision      = 0u;
	u32             m_PendingExtractionRevision = 0u;
	u32             m_PendingRevision           = kInvalidIndex;
	bool            m_bHasPending              = false;

	u32  m_NumChunks         = 0u;
	u32  m_ChunkSlabId       = kInvalidIndex; // reused across rebuilds (single chunk)
	u32  m_BuiltRevision     = kInvalidIndex; // last built revision (sentinel => first frame builds)
	u32  m_RejectedRevision  = kInvalidIndex;

	// --- Pipelines ---
	Box< render::ComputePipeline >  m_pChunkCullingPSO;
	Box< render::GraphicsPipeline > m_pVoxelGBufferPSO;
	Box< render::ComputePipeline >  m_pMCExtractPSO;
	Box< render::ComputePipeline >  m_pMeshletBuildPSO;

	// --- GPU density volume (R32, dim^3) ---
	Arc< render::Texture >          m_pDensityVolume; // written by VoxelDensityCS; MC reads the linear copy below
	Arc< render::Buffer >           m_pDensityField;  // linear density copy the MC extract samples
	Box< render::ComputePipeline >  m_pDensityPSO;

	// --- GPU marching-cubes extract + meshlet build ---
	Arc< render::Buffer > m_pMCTriTable; // 256x16 MC triangle-edge table (SSBO, uploaded once)
	Arc< render::Buffer > m_pMCCounter;  // [triangleCount, activeCellCount]
	bool m_bTriTableUploaded   = false;  // tri-table SSBO uploaded (global, once)
	bool m_bSlabStaticUploaded = false;  // identity/pattern index arrays uploaded (per slab)
};


} // namespace baamboo
