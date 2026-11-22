#include "BaambooPch.h"
#include "GBufferNode.h"
#include "CullingNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "RenderCommon/CpuProfiler.h"
#include "RenderCommon/SceneResource.h"
#include "BaambooScene/Scene.h"
#include "PipelineFactory.h"

namespace baamboo
{


GBufferNode::GBufferNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory)
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
		                  .AttachTexture(eAttachmentPoint::DepthStencil, pAttachmentDepth).Build();

	// Phase 2 render target (LOAD all attachments - same textures, different load ops)
	m_pRenderTargetPhase2 = RenderTarget::CreateEmpty(rd, "GBufferPass::RenderPassPhase2");
	m_pRenderTargetPhase2->AttachTexture(eAttachmentPoint::Color0, pAttachmentVBuf0)
		                  .AttachTexture(eAttachmentPoint::Color1, pAttachmentVBuf1)
		                  .AttachTexture(eAttachmentPoint::DepthStencil, pAttachmentDepth)
		                  .SetLoadAttachment(eAttachmentPoint::Color0)
		                  .SetLoadAttachment(eAttachmentPoint::Color1)
		                  .SetLoadAttachment(eAttachmentPoint::DepthStencil).Build();

	// --- GBuffer PSO ---
	m_pGBufferPSO = pipelineFactory.CreateMeshPipeline("GBufferPSO",
		{
			.task  = { .source = "GBufferTS.slang" },
			.mesh  = { .source = "GBufferMS.slang" },
			.pixel = { .source = "GBufferPS.slang" },
		});
	m_pGBufferPSO->SetRenderTarget(m_pRenderTargetPhase1)
		          .SetDepthWriteEnable(true, eCompareOp::Greater).Build();

	auto MakeFallback = [&rd](const char* name, u64 elemSize) -> Arc< Buffer >
	{
		return Buffer::Create(rd, name,
			{
				.count              = 1,
				.elementSizeInBytes = elemSize,
				.bufferUsage        = eBufferUsage_Storage,
			});
	};
	m_pVoxelVertexFallback          = MakeFallback("GBufferPass::VoxelVertexFallback", sizeof(VoxelVertex));
	m_pVoxelMeshletFallback         = MakeFallback("GBufferPass::VoxelMeshletFallback", sizeof(Meshlet));
	m_pVoxelMeshletVertexFallback   = MakeFallback("GBufferPass::VoxelMeshletVertexFallback", sizeof(u32));
	m_pVoxelMeshletTriangleFallback = MakeFallback("GBufferPass::VoxelMeshletTriangleFallback", sizeof(u32));
	m_pVoxelChunkDescFallback       = MakeFallback("GBufferPass::VoxelChunkDescFallback", sizeof(VoxelChunkDesc));

	m_pErosionDetailFallback = Texture::Create(rd, "GBufferPass::ErosionDetailFallback",
		{
			.imageType  = eImageType::Texture2DArray, // must match the Texture2DArray shader declaration
			.resolution = uint3(1, 1, 1),
			.format     = eFormat::RGBA16_FLOAT,
			.imageUsage = eTextureUsage_Sample,
		});

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
	g_FrameData.pDepth    = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::DepthStencil);
}

// Shared phase-1/phase-2 body: stage voxel/mesh resources and emit the indirect mesh-task draw.
void GBufferNode::DrawGBufferImpl(render::CommandContext& context, Arc< render::RenderTarget > rt, const MeshCullOutputs& cullOutputs)
{
	using namespace render;

	BAAMBOO_GPU_SCOPE(context,
		cullOutputs.phase == kCullPhase2 ? "GBuffer.Phase2" : "GBuffer.Phase1");

	const bool bHasInstances = cullOutputs.numInstances > 0;

	auto pVoxVerts    = g_FrameData.pVoxelVertices.lock();
	auto pVoxMeshlets = g_FrameData.pVoxelMeshlets.lock();
	auto pVoxMv       = g_FrameData.pVoxelMeshletVertices.lock();
	auto pVoxMt       = g_FrameData.pVoxelMeshletTriangles.lock();
	auto pVoxDescs    = g_FrameData.pVoxelChunkDescs.lock();
	auto pErosion     = g_FrameData.pVoxelErosionDetail.lock();

	if (bHasInstances)
	{
		if (cullOutputs.phase == kCullPhase2)
		{
			context.TransitionTextureToRead(cullOutputs.pHiZ, ePipelineStage::TaskShader);
		}
		context.TransitionBufferToWrite(cullOutputs.pMeshletVisibility, ePipelineStage::TaskShader);

		if (pVoxVerts)    context.TransitionBufferToRead(pVoxVerts,    ePipelineStage::TaskShader | ePipelineStage::MeshShader);
		if (pVoxMeshlets) context.TransitionBufferToRead(pVoxMeshlets, ePipelineStage::TaskShader | ePipelineStage::MeshShader);
		if (pVoxMv)       context.TransitionBufferToRead(pVoxMv,       ePipelineStage::TaskShader | ePipelineStage::MeshShader);
		if (pVoxMt)       context.TransitionBufferToRead(pVoxMt,       ePipelineStage::TaskShader | ePipelineStage::MeshShader);
		if (pVoxDescs)    context.TransitionBufferToRead(pVoxDescs,    ePipelineStage::TaskShader | ePipelineStage::MeshShader);

		context.TransitionTextureToRead(pErosion ? pErosion : m_pErosionDetailFallback, ePipelineStage::MeshShader);

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

		const GBufferConstants constants = {
			.viewport = float2(static_cast< float >(m_RenderDevice.WindowWidth()), static_cast< float >(m_RenderDevice.WindowHeight())),
			.phase    = cullOutputs.phase,
		};
		context.SetGraphicsConstants(sizeof(constants), &constants);

		context.StageDescriptor("ID_Scene", m_RenderDevice.GetResourceManager().GetSceneResource().GetSceneGlobals());
		context.StageDescriptor("ID_DrawIDs", cullOutputs.pDrawIndex);
		context.StageDescriptor("ID_HiZTexture", cullOutputs.pHiZ, g_FrameData.pLinearClampMin);
		context.StageDescriptor("ID_MeshletVisibility", cullOutputs.pMeshletVisibility);
#if PROFILING_LEVEL >= 1
		if (cullOutputs.pMeshletStats)
			context.StageDescriptor("ID_MeshletStats", cullOutputs.pMeshletStats);
#endif

		context.StageDescriptor("ID_VoxelVertices",         pVoxVerts    ? pVoxVerts    : m_pVoxelVertexFallback);
		context.StageDescriptor("ID_VoxelMeshlets",         pVoxMeshlets ? pVoxMeshlets : m_pVoxelMeshletFallback);
		context.StageDescriptor("ID_VoxelMeshletVertices",  pVoxMv       ? pVoxMv       : m_pVoxelMeshletVertexFallback);
		context.StageDescriptor("ID_VoxelMeshletTriangles", pVoxMt       ? pVoxMt       : m_pVoxelMeshletTriangleFallback);

		context.StageDescriptor("ID_VoxelChunkDescs", pVoxDescs ? pVoxDescs : m_pVoxelChunkDescFallback);
		context.StageDescriptor("ID_ErosionDetailMap", pErosion ? pErosion : m_pErosionDetailFallback, g_FrameData.pLinearClamp);

		context.DrawMeshTasksIndirectCount(
			cullOutputs.pIndirectCommands,
			0,
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
