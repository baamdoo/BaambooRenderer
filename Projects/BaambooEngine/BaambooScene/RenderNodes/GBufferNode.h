#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

class GBufferNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	GBufferNode(render::RenderDevice& rd);
	virtual ~GBufferNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	static constexpr u32 PHASE1_CULL  = 0u;
	static constexpr u32 PHASE2_CULL  = 1u;

	void DispatchCull(render::CommandContext& context, u32 numInstances, u32 phase);
	void DrawGBuffer(render::CommandContext& context, Arc< render::RenderTarget > rt, u32 numInstances, u32 phase);
	void BuildHiZ(render::CommandContext& context);

private:
	// Draw buffers
	Arc< render::Buffer > m_DrawIndexBuffer;
	Arc< render::Buffer > m_DrawCountBuffer;
	Arc< render::Buffer > m_CulledIndirectCommandBuffer;

	// GBuffer render targets
	Arc< render::RenderTarget > m_pRenderTargetPhase1; // Phase 1: clear all
	Arc< render::RenderTarget > m_pRenderTargetPhase2; // Phase 2: load all

	Arc< render::Texture > m_pHiZTexture;

	Arc< render::Buffer > m_pSPDCounterBuffer;
	Arc< render::Buffer > m_VisibilityBuffer;
	Arc< render::Buffer > m_MeshletVisibilityBuffer;
	u32                   m_NumMeshletVisibilityWords = 0;

	static constexpr u32 NUM_INITIAL_MESHLET_VISIBILITY_WORDS = _KB(2);

	// Pipelines
	Box< render::ComputePipeline >  m_pInstanceCullingPSO;
	Box< render::ComputePipeline >  m_pHiZGenerationPSO;
	Box< render::GraphicsPipeline > m_pGBufferPSO;

	bool m_bNeedsClear = true;

	static constexpr u32  READBACK_SLOTS = MAX_FRAMES_IN_FLIGHT;
	Arc< render::Buffer > m_Phase1CountReadback;
	Arc< render::Buffer > m_Phase2CountReadback;

#if PROFILING_LEVEL >= 1
	static constexpr u32  MESHLET_STATS_FIELDS = 3;
	Arc< render::Buffer > m_MeshletStatsBuffer;
	Arc< render::Buffer > m_Phase1MeshletStatsReadback;
	Arc< render::Buffer > m_Phase2MeshletStatsReadback;
#endif

	u32 m_ReadbackIdx          = 0;
	u32 m_ReadbackFrameCounter = 0;
};

} // namespace baamboo
