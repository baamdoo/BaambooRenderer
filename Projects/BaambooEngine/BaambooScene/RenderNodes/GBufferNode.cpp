#include "BaambooPch.h"
#include "GBufferNode.h"
#include "CullingNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "RenderCommon/CpuProfiler.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{


GBufferNode::GBufferNode(render::RenderDevice& rd)
	: Super(rd, "GBufferPass")
{
	using namespace render;

	auto pAttachmentVBuf0 =
		Texture::Create(rd, "GBufferPass::VBuf0/SurfaceID",
			{
				.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
				.format     = eFormat::R32_UINT,
				.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample
			});
	auto pAttachmentVBuf1 =
		Texture::Create(rd, "GBufferPass::VBuf1/PrimitiveID",
			{
				.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
				.format     = eFormat::R32_UINT,
				.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample
			});
	auto pAttachmentVelocity =
		Texture::Create(rd, "GBufferPass::Velocity",
			{
				.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
				.format     = eFormat::RG16_FLOAT,
				.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample
			});
	auto pAttachmentDepth =
		Texture::Create(rd, "GBufferPass::AttachmentDepth",
			{
				.resolution      = { rd.WindowWidth(), rd.WindowHeight(), 1 },
				.format          = eFormat::D32_FLOAT,
				.imageUsage      = eTextureUsage_DepthStencilAttachment | eTextureUsage_Sample,
				.depthClearValue = 0.0f // reversed-z
			});

	// Phase 1 render target (CLEAR all attachments)
	m_pRenderTargetPhase1 = RenderTarget::CreateEmpty(rd, "GBufferPass::RenderPass");
	m_pRenderTargetPhase1->AttachTexture(eAttachmentPoint::Color0, pAttachmentVBuf0)
		                  .AttachTexture(eAttachmentPoint::Color1, pAttachmentVBuf1)
		                  .AttachTexture(eAttachmentPoint::Color2, pAttachmentVelocity)
		                  .AttachTexture(eAttachmentPoint::DepthStencil, pAttachmentDepth).Build();

	// Phase 2 render target (LOAD all attachments — same textures, different load ops)
	m_pRenderTargetPhase2 = RenderTarget::CreateEmpty(rd, "GBufferPass::RenderPassPhase2");
	m_pRenderTargetPhase2->AttachTexture(eAttachmentPoint::Color0, pAttachmentVBuf0)
		                  .AttachTexture(eAttachmentPoint::Color1, pAttachmentVBuf1)
		                  .AttachTexture(eAttachmentPoint::Color2, pAttachmentVelocity)
		                  .AttachTexture(eAttachmentPoint::DepthStencil, pAttachmentDepth)
		                  .SetLoadAttachment(eAttachmentPoint::Color0)
		                  .SetLoadAttachment(eAttachmentPoint::Color1)
		                  .SetLoadAttachment(eAttachmentPoint::Color2)
		                  .SetLoadAttachment(eAttachmentPoint::DepthStencil).Build();

	// --- GBuffer PSO ---
	m_pGBufferPSO = GraphicsPipeline::Create(rd, "GBufferPSO");
	if (!rd.GetDeviceSettings().bMeshShader)
	{
		auto pVS = Shader::Create(rd, "GBufferVS", { .stage = eShaderStage::Vertex, .filename = "GBufferVS" });
		auto pFS = Shader::Create(rd, "GBufferPS", { .stage = eShaderStage::Fragment, .filename = "GBufferPS" });

		m_pGBufferPSO->SetShaders(pVS, pFS)
			          .SetRenderTarget(m_pRenderTargetPhase1)
			          .SetDepthWriteEnable(true, eCompareOp::Greater).Build();
	}
	else
	{
		auto pTS = Shader::Create(rd, "GBufferTS", { .stage = eShaderStage::Task, .filename = "GBufferTS" });
		auto pMS = Shader::Create(rd, "GBufferMS", { .stage = eShaderStage::Mesh, .filename = "GBufferMS" });
		auto pFS = Shader::Create(rd, "GBufferPS", { .stage = eShaderStage::Fragment, .filename = "GBufferPS" });

		m_pGBufferPSO->SetMeshShaders(pMS, pFS, pTS)
			          .SetRenderTarget(m_pRenderTargetPhase1)
			          .SetDepthWriteEnable(true, eCompareOp::Greater).Build();
	}

	//
	g_FrameData.pPhase2Draw = m_pRenderTargetPhase2;
}

void GBufferNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(context);
	UNUSED(renderView);
}

void GBufferNode::DrawGBufferPhase1(render::CommandContext& context, const MeshCullOutputs& cullOutputs)
{
	DrawGBufferImpl(context, m_pRenderTargetPhase1, cullOutputs);
	m_pRenderTargetPhase1->InvalidateImageLayout();
}

void GBufferNode::DrawGBufferPhase2(render::CommandContext& context, const MeshCullOutputs& cullOutputs)
{
	using namespace render;

	DrawGBufferImpl(context, m_pRenderTargetPhase2, cullOutputs);
	m_pRenderTargetPhase2->InvalidateImageLayout();

	g_FrameData.pVBuf0    = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::Color0);
	g_FrameData.pVBuf1    = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::Color1);
	g_FrameData.pVelocity = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::Color2);
	g_FrameData.pDepth    = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::DepthStencil);
}

// =========================================================================
// DrawGBufferImpl — emit indirect draw.
// =========================================================================
void GBufferNode::DrawGBufferImpl(render::CommandContext& context, Arc< render::RenderTarget > rt, const MeshCullOutputs& cullOutputs)
{
	using namespace render;

	const bool bHasInstances = cullOutputs.numInstances > 0;
	if (bHasInstances)
	{
		if (cullOutputs.phase == CullingNode::kPhase2Cull)
		{
			context.TransitionBarrier(cullOutputs.pHiZ, eTextureLayout::ShaderReadOnly);
		}
		context.TransitionBufferToWrite(cullOutputs.pMeshletVisibility, ePipelineStage::TaskShader);

#if PROFILING_LEVEL >= 1
		if (cullOutputs.pMeshletStats)
		{
			context.ClearBuffer(cullOutputs.pMeshletStats, 0);
			context.TransitionBufferToWrite(cullOutputs.pMeshletStats, ePipelineStage::TaskShader);
		}
#endif
	}

	context.BeginRenderPass(rt);
	if (bHasInstances)
	{
		context.SetRenderPipeline(m_pGBufferPSO.get());

		struct GBufferPushConstants
		{
			float viewportWidth;
			float viewportHeight;
			u32   phase;
		} constants = {
			.viewportWidth  = static_cast< float >(m_RenderDevice.WindowWidth()),
			.viewportHeight = static_cast< float >(m_RenderDevice.WindowHeight()),
			.phase          = cullOutputs.phase,
		};
		context.SetConstants(sizeof(constants), &constants, static_cast< eShaderStage >(eShaderStage::Task | eShaderStage::Mesh));
		context.SetGraphicsShaderResource("g_DrawIDs", cullOutputs.pDrawIndex);
		context.StageDescriptor("g_HiZTexture", cullOutputs.pHiZ, g_FrameData.pLinearClampMin);
		context.StageDescriptor("g_MeshletVisibilityBuffer", cullOutputs.pMeshletVisibility);
#if PROFILING_LEVEL >= 1
		if (cullOutputs.pMeshletStats)
			context.StageDescriptor("g_MeshletStats", cullOutputs.pMeshletStats);
#endif

		context.DrawMeshTasksIndirectCount(
			cullOutputs.pIndirectCommands,
			offsetof(IndirectCommandData, groupCountX),
			cullOutputs.pDrawCount,
			cullOutputs.numInstances,
			sizeof(IndirectCommandData)
		);
	}
	context.EndRenderPass();
}

Arc< render::Texture > GBufferNode::GetDepthAttachment() const
{
	return m_pRenderTargetPhase1 ? m_pRenderTargetPhase1->Attachment(render::eAttachmentPoint::DepthStencil) : nullptr;
}

void GBufferNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pRenderTargetPhase1)
		m_pRenderTargetPhase1->Resize(width, height, depth);
	if (m_pRenderTargetPhase2)
		m_pRenderTargetPhase2->Resize(width, height, depth);
}


} // namespace baamboo
