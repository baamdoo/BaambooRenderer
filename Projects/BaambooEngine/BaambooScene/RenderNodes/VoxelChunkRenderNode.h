#pragma once
#include "RenderCommon/RenderNode.h"
#include "BaambooScene/VoxelTerrain/VoxelTerrainTypes.h"
#include "BaambooScene/VoxelTerrain/TerrainMeshData.h"

struct VoxelTerrainRenderView;

namespace baamboo
{


class VoxelChunkRenderNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	static constexpr u32 kMaxChunks        = 1024u;
	static constexpr u32 kMaxResidentSlabs = 8u;
	// Per-slab capacities — sized for a worst-case 32^3 oracle chunk + headroom.
	static constexpr u32 kVertexSlabCapacity   = 98304u;  // verts / chunk
	static constexpr u32 kMeshletVertexSlabCap = 131072u; // meshlet-local vertex indices
	static constexpr u32 kMeshletTriSlabCap    = 131072u; // packed triangle words
	static constexpr u32 kMeshletSlabCapacity  = 4096u;   // meshlets / chunk

	VoxelChunkRenderNode(render::RenderDevice& rd);
	virtual ~VoxelChunkRenderNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

	// --- Driven by CullingNode ---
	void DispatchChunkCull(render::CommandContext& context, u32 phase, const Arc< render::Texture >& pHiZTexture, const SceneRenderView& renderView);
	void DrawChunksPhase1(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView);
	void DrawChunksPhase2(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView);

private:
	bool SetOracleChunk(const VoxelTerrainChunkDesc& desc, const VoxelTerrainRenderView& terrainView);
	bool EnsureUpload(render::CommandContext& context);
	u32  AllocateSlab();
	void DiscardPending(u32 rejectedRevision);
	void DrawChunksImpl(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView);
	void PublishPools();

private:
	// --- Persistent geometry pools (device-local storage; never per-frame reset) ---
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

	// --- Pending CPU upload ---
	TerrainMeshData m_PendingMesh;
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

	u32  m_NumChunks        = 0u;
	u32  m_ChunkMeshletCount = 0u; // CPU-known meshlet count of the resident chunk (Phase 1 emit)
	u32  m_ChunkSlabId      = kInvalidIndex; // reused across rebuilds (single chunk)
	u32  m_BuiltRevision    = kInvalidIndex; // VoxelTerrainRenderView::revision last built (sentinel => first frame builds)
	u32  m_RejectedRevision = kInvalidIndex;

	// --- Pipelines ---
	Box< render::ComputePipeline >  m_pChunkCullingPSO;
	Box< render::GraphicsPipeline > m_pVoxelGBufferPSO;
};


} // namespace baamboo
