#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{


struct MeshCullOutputs;

class GBufferNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	GBufferNode(render::RenderDevice& rd);
	virtual ~GBufferNode() = default;

	// No-op: GBufferNode is not registered in RenderGraph. CullingNode owns this and drives DrawGBufferPhase1/2.
	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

	// --- Draw entry points (called by CullingNode) ---
	void DrawGBufferPhase1(render::CommandContext& context, const MeshCullOutputs& cullOutputs);
	void DrawGBufferPhase2(render::CommandContext& context, const MeshCullOutputs& cullOutputs);

	Arc< render::Texture > GetDepthAttachment() const;

	Arc< render::RenderTarget > GetPhase2RenderTarget() const { return m_pRenderTargetPhase2; }

private:
	void DrawGBufferImpl(render::CommandContext& context, Arc< render::RenderTarget > rt, const MeshCullOutputs& cullOutputs);

	Arc< render::RenderTarget > m_pRenderTargetPhase1; // CLEAR all attachments
	Arc< render::RenderTarget > m_pRenderTargetPhase2; // LOAD all attachments (same textures, different load ops)

	Box< render::GraphicsPipeline > m_pGBufferPSO;

	Arc< render::Buffer > m_pVoxelVertexFallback;
	Arc< render::Buffer > m_pVoxelMeshletFallback;
	Arc< render::Buffer > m_pVoxelMeshletVertexFallback;
	Arc< render::Buffer > m_pVoxelMeshletTriangleFallback;

	Arc< render::Texture > m_pErosionDetailFallback;

};


} // namespace baamboo
