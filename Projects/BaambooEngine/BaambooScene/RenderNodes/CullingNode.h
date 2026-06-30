#pragma once
#include "GBufferNode.h"
#include "VoxelChunkRenderNode.h"

namespace baamboo
{


struct MeshCullOutputs
{
	Arc< render::Buffer >  pIndirectCommands;
	Arc< render::Buffer >  pDrawCount;
	Arc< render::Buffer >  pDrawIndex;
	Arc< render::Buffer >  pMeshletVisibility;
	Arc< render::Buffer >  pMeshletStats;
	Arc< render::Texture > pHiZ;

	u32 numInstances = 0u;
	u32 phase        = 0u;
};


class CullingNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	static constexpr u32 kPhase1Cull = 0u;
	static constexpr u32 kPhase2Cull = 1u;

	CullingNode(render::RenderDevice& rd);
	virtual ~CullingNode();

	CullingNode(const CullingNode& other);
	CullingNode& operator=(const CullingNode& other);
	CullingNode(CullingNode&& other) noexcept;
	CullingNode& operator=(CullingNode&& other) noexcept;

	// --- Sub-node registration (app constructs both, links here) ---
	void SetGBufferNode(const Arc< GBufferNode >& pNode) { m_pGBufferNode = pNode; }
	void SetVoxelNode(const Arc< VoxelChunkRenderNode >& pNode) { m_pVoxelNode = pNode; }

	// --- Shared HiZ exposure (for future SSAO/reflections that need depth pyramid) ---
	Arc< render::Texture > GetHiZTexture() const { return m_pHiZTexture; }

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	void DispatchMeshCull(render::CommandContext& context, u32 numInstances, u32 phase);
	void PatchVoxelMeshData(render::CommandContext& context, const SceneRenderView& renderView);
	void BuildHiZ(render::CommandContext& context);
	void EnsureMeshletVisibility(u32 numRequiredWords);
	void PublishReadbackStats();

	MeshCullOutputs MakeMeshCullOutputs(u32 numInstances, u32 phase) const;

private:
	// --- Sub-nodes (draw responsibility, owned via Arc) ---
	Arc< GBufferNode >          m_pGBufferNode;
	Arc< VoxelChunkRenderNode > m_pVoxelNode;

	// --- Mesh cull resources ---
	Arc< render::Buffer > m_DrawIndexBuffer;
	Arc< render::Buffer > m_DrawCountBuffer;
	Arc< render::Buffer > m_CulledIndirectCommandBuffer;
	Arc< render::Buffer > m_VisibilityBuffer;
	Arc< render::Buffer > m_MeshletVisibilityBuffer;
	u32                   m_NumMeshletVisibilityWords = 0;
	static constexpr u32  kNumInitialMeshletVisibilityWords = _KB(2);

	// --- HiZ pyramid + SPD ---
	Arc< render::Texture > m_pHiZTexture;
	Arc< render::Buffer >  m_pSPDCounterBuffer;

	// --- Compute pipelines ---
	Box< render::ComputePipeline > m_pInstanceCullingPSO;
	Box< render::ComputePipeline > m_pHiZGenerationPSO;
	Box< render::ComputePipeline > m_pVoxelMeshDataPatchPSO; // stamps GPU meshlet count into the voxel instance's MeshData

	// --- Readback ring ---
	static constexpr u32  kReadbackSlots = kMaxFramesInFlight;
	Arc< render::Buffer > m_Phase1CountReadback;
	Arc< render::Buffer > m_Phase2CountReadback;

#if PROFILING_LEVEL >= 1
	static constexpr u32  kMeshletStatsFields = 3;
	Arc< render::Buffer > m_MeshletStatsBuffer;
	Arc< render::Buffer > m_Phase1MeshletStatsReadback;
	Arc< render::Buffer > m_Phase2MeshletStatsReadback;
#endif

	bool m_bNeedsClear          = true;
	u32  m_ReadbackIdx          = 0;
	u32  m_ReadbackFrameCounter = 0;
};

} // namespace baamboo
