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
	// Occlusion culling constants matching HLSL defines
	static constexpr u32 PHASE1_CULL  = 0u;
	static constexpr u32 PHASE2_CULL  = 1u;

	struct CullPushConstants
	{
		u32 numInstances;
		u32 cullingPhase;
		u32 hiZMipCount;
		u32 hiZWidth;
		u32 hiZHeight;
	};

	void DispatchCull(render::CommandContext& context, u32 numInstances, u32 phase);
	void DrawGBuffer(render::CommandContext& context, Arc< render::RenderTarget > rt, u32 numInstances);
	void BuildHiZ(render::CommandContext& context);

private:
	// Draw buffers
	Arc< render::Buffer >       m_DrawIndexBuffer;
	Arc< render::Buffer >       m_DrawCountBuffer;
	Arc< render::Buffer >       m_CulledIndirectCommandBuffer;

	// GBuffer render targets
	Arc< render::RenderTarget > m_pRenderTargetPhase1;       // Phase 1: clear all
	Arc< render::RenderTarget > m_pRenderTargetPhase2; // Phase 2: load all

	// HiZ pyramid (R32_FLOAT mipmapped texture, rebuilt each frame)
	Arc< render::Texture >      m_pHiZTexture;

	// SPD atomic counter for Hi-Z single-pass downsampling
	Arc< render::Buffer >       m_pSPDCounterBuffer;

	// Visibility bitfield (1 bit per instance, packed in u32s)
	// Persists across frames: Phase 1 reads prev-frame bits, Phase 2 writes new bits
	Arc< render::Buffer >       m_VisibilityBuffer;

	// Pipelines
	Box< render::ComputePipeline >  m_pInstanceCullingPSO;
	Box< render::ComputePipeline >  m_pHiZGenerationPSO;
	Box< render::GraphicsPipeline > m_pGBufferPSO;

	bool m_bNeedsClear = true;
};

} // namespace baamboo
