#include "BaambooPch.h"
#include "GBufferNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "RenderCommon/CpuProfiler.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{

namespace
{

u32 previousPow2(u32 v)
{
	u32 r = 1u;
	while (r * 2u <= v)
		r *= 2u;
	return r;
}

}


GBufferNode::GBufferNode(render::RenderDevice& rd)
	: Super(rd, "GBufferPass")
{
	using namespace render;

	// --- Draw buffers ---
	m_DrawIndexBuffer = Buffer::Create(rd, "GBufferPass::DrawIndexBuffer",
		{
			.count              = MAX_ENTITY_COUNT,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage
		});
	m_DrawCountBuffer = Buffer::Create(rd, "GBufferPass::DrawCountBuffer",
		{
			.count              = 1,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_Indirect | eBufferUsage_TransferSource | eBufferUsage_TransferDest | eBufferUsage_ShaderDeviceAddress,
		});
	m_CulledIndirectCommandBuffer = Buffer::Create(rd, "GBufferPass::CulledIndirectCommandBuffer",
		{
			.count              = MAX_ENTITY_COUNT,
			.elementSizeInBytes = sizeof(IndirectCommandData),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_Indirect
		});
	m_VisibilityBuffer = Buffer::Create(rd, "GBufferPass::VisibilityBuffer",
		{
			.count              = MAX_ENTITY_COUNT,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest
		});
	m_MeshletVisibilityBuffer = Buffer::Create(rd, "GBufferPass::MeshletVisibilityBuffer",
		{
			.count              = NUM_INITIAL_MESHLET_VISIBILITY_WORDS,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferSource | eBufferUsage_TransferDest
		});
	m_NumMeshletVisibilityWords = NUM_INITIAL_MESHLET_VISIBILITY_WORDS;
	m_pSPDCounterBuffer = Buffer::Create(rd, "GBufferPass::SPDCounterBuffer",
		{
			.count              = 1,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	// Debug culling
	m_Phase1CountReadback = Buffer::Create(rd, "GBufferPass::Phase1CountReadback",
		{
			.count              = READBACK_SLOTS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
	m_Phase2CountReadback = Buffer::Create(rd, "GBufferPass::Phase2CountReadback",
		{
			.count              = READBACK_SLOTS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});

#if PROFILING_LEVEL >= 1
	m_MeshletStatsBuffer = Buffer::Create(rd, "GBufferPass::MeshletStatsBuffer",
		{
			.count              = MESHLET_STATS_FIELDS,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferSource | eBufferUsage_TransferDest,
		});
	m_Phase1MeshletStatsReadback = Buffer::Create(rd, "GBufferPass::Phase1MeshletStatsReadback",
		{
			.count              = READBACK_SLOTS * MESHLET_STATS_FIELDS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
	m_Phase2MeshletStatsReadback = Buffer::Create(rd, "GBufferPass::Phase2MeshletStatsReadback",
		{
			.count              = READBACK_SLOTS * MESHLET_STATS_FIELDS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
#endif // PROFILING_LEVEL >= 1

	// --- GBuffer attachments ---
	auto pAttachment0 =
		Texture::Create(rd, "GBufferPass::Attachment0/RGB_Albedo/A_AO",
			{
				.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
				.format     = eFormat::RGBA8_UNORM,
				.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample | eTextureUsage_TransferSource
			});
	auto pAttachment1 =
		Texture::Create(rd, "GBufferPass::Attachment1/RGB_Normal/A_MaterialID",
			{
				.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
				.format     = eFormat::RGBA8_SNORM,
				.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample
			});
	auto pAttachment2 =
		Texture::Create(rd, "GBufferPass::Attachment2/RGB_Emissive",
			{
				.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
				.format     = eFormat::RG11B10_UFLOAT,
				.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample
			});
	auto pAttachment3 =
		Texture::Create(rd, "GBufferPass::Attachment3/RG_Velocity/B_Roughness/A_Metallic",
			{
				.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
				.format     = eFormat::RGBA16_FLOAT,
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
	m_pRenderTargetPhase1->AttachTexture(eAttachmentPoint::Color0, pAttachment0)
		                  .AttachTexture(eAttachmentPoint::Color1, pAttachment1)
		                  .AttachTexture(eAttachmentPoint::Color2, pAttachment2)
		                  .AttachTexture(eAttachmentPoint::Color3, pAttachment3)
		                  .AttachTexture(eAttachmentPoint::DepthStencil, pAttachmentDepth).Build();

	// Phase 2 render target (LOAD all attachments — same textures, different load ops)
	m_pRenderTargetPhase2 = RenderTarget::CreateEmpty(rd, "GBufferPass::RenderPassPhase2");
	m_pRenderTargetPhase2->AttachTexture(eAttachmentPoint::Color0, pAttachment0)
		                  .AttachTexture(eAttachmentPoint::Color1, pAttachment1)
		                  .AttachTexture(eAttachmentPoint::Color2, pAttachment2)
		                  .AttachTexture(eAttachmentPoint::Color3, pAttachment3)
		                  .AttachTexture(eAttachmentPoint::DepthStencil, pAttachmentDepth)
		                  .SetLoadAttachment(eAttachmentPoint::Color0)
		                  .SetLoadAttachment(eAttachmentPoint::Color1)
		                  .SetLoadAttachment(eAttachmentPoint::Color2)
		                  .SetLoadAttachment(eAttachmentPoint::Color3)
		                  .SetLoadAttachment(eAttachmentPoint::DepthStencil).Build();

	// Size to previousPow2 of the window so every SPD reduction is exactly 2x2.
	m_pHiZTexture = Texture::Create(rd, "GBufferPass::HiZTexture",
		{
			.resolution    = { previousPow2(rd.WindowWidth()), previousPow2(rd.WindowHeight()), 1 },
			.format        = eFormat::R32_FLOAT,
			.imageUsage    = eTextureUsage_Sample | eTextureUsage_Storage,
			.bGenerateMips = true,
		});


	// --- Pipelines ---
	auto pCullingCS = Shader::Create(rd, "InstanceCullingCS", { .stage = eShaderStage::Compute, .filename = "InstanceCullingCS" });
	m_pInstanceCullingPSO = ComputePipeline::Create(rd, "InstanceCullingPSO");
	m_pInstanceCullingPSO->SetComputeShader(pCullingCS).Build();

	auto pHiZGenerationCS = Shader::Create(rd, "HiZGenerationCS", { .stage = eShaderStage::Compute, .filename = "HiZGenerationCS" });
	m_pHiZGenerationPSO = ComputePipeline::Create(rd, "HiZGenerationPSO");
	m_pHiZGenerationPSO->SetComputeShader(pHiZGenerationCS).Build();

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
}

// =========================================================================
// Dispatch instance culling compute shader for a given phase
// =========================================================================
void GBufferNode::DispatchCull(render::CommandContext& context, u32 numInstances, u32 phase)
{
	using namespace render;

	context.ClearBuffer(m_DrawCountBuffer, 0);

	context.SetRenderPipeline(m_pInstanceCullingPSO.get());

	context.TransitionBufferToWrite(m_CulledIndirectCommandBuffer, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_DrawCountBuffer, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_DrawIndexBuffer, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_VisibilityBuffer, ePipelineStage::ComputeShader);
	context.TransitionBarrier(m_pHiZTexture, eTextureLayout::ShaderReadOnly);

	struct
	{
		u32 numInstances;
		u32 cullingPhase;
		u32 hiZMipCount;
		u32 hiZWidth;
		u32 hiZHeight;
		u32 cullFlags;
	} constant = {
		.numInstances = numInstances,
		.cullingPhase = phase,
		.hiZMipCount  = m_pHiZTexture->MipLevels(),
		.hiZWidth     = m_pHiZTexture->Width(),
		.hiZHeight    = m_pHiZTexture->Height(),
		.cullFlags    = g_FrameData.cullFlags,
	};
	context.SetComputeConstants(sizeof(constant), &constant);

	context.StageDescriptor("g_IndirectCommands", m_CulledIndirectCommandBuffer);
	context.StageDescriptor("g_DrawIDs", m_DrawIndexBuffer);
	context.StageDescriptor("g_DrawCount", m_DrawCountBuffer);
	context.StageDescriptor("g_VisibilityBuffer", m_VisibilityBuffer);
	context.StageDescriptor("g_HiZTexture", m_pHiZTexture, g_FrameData.pLinearClampMin);

	context.Dispatch1D< 64 >(numInstances);

	context.TransitionBufferToRead(m_CulledIndirectCommandBuffer, ePipelineStage::DrawIndirect);
	context.TransitionBufferToRead(m_DrawCountBuffer, ePipelineStage::DrawIndirect);
	context.TransitionBufferToRead(m_DrawIndexBuffer, ePipelineStage::TaskShader, 0, true);
}

// =========================================================================
// Draw GBuffer
// =========================================================================
void GBufferNode::DrawGBuffer(render::CommandContext& context, Arc< render::RenderTarget > rt, u32 numInstances, u32 phase)
{
	using namespace render;

	if (phase == PHASE2_CULL)
	{
		context.TransitionBarrier(m_pHiZTexture, eTextureLayout::ShaderReadOnly);
	}
	context.TransitionBufferToWrite(m_MeshletVisibilityBuffer, ePipelineStage::TaskShader);

#if PROFILING_LEVEL >= 1
	context.ClearBuffer(m_MeshletStatsBuffer, 0);
	context.TransitionBufferToWrite(m_MeshletStatsBuffer, ePipelineStage::TaskShader);
#endif

	context.BeginRenderPass(rt);
	{
		context.SetRenderPipeline(m_pGBufferPSO.get());

		struct GBufferPushConstants
		{
			float viewportWidth;
			float viewportHeight;

			u32 cullFlags;
			u32 phase;
			u32 hiZWidth;
			u32 hiZHeight;
		} constants = {
			.viewportWidth  = static_cast<float>(m_RenderDevice.WindowWidth()),
			.viewportHeight = static_cast<float>(m_RenderDevice.WindowHeight()),

			.cullFlags = g_FrameData.cullFlags,
			.phase     = phase,
			.hiZWidth  = m_pHiZTexture->Width(),
			.hiZHeight = m_pHiZTexture->Height(),
		};
		context.SetConstants(sizeof(constants), &constants, static_cast<eShaderStage>(eShaderStage::Task | eShaderStage::Mesh));
		context.SetGraphicsShaderResource("g_DrawIDs", m_DrawIndexBuffer);
		context.StageDescriptor("g_HiZTexture", m_pHiZTexture, g_FrameData.pLinearClampMin);
		context.StageDescriptor("g_MeshletVisibilityBuffer", m_MeshletVisibilityBuffer);
#if PROFILING_LEVEL >= 1
		context.StageDescriptor("g_MeshletStats", m_MeshletStatsBuffer);
#endif

		context.DrawMeshTasksIndirectCount(
			m_CulledIndirectCommandBuffer,
			offsetof(IndirectCommandData, groupCountX),
			m_DrawCountBuffer,
			numInstances,
			sizeof(IndirectCommandData)
		);
	}
	context.EndRenderPass();

#if PROFILING_LEVEL >= 1
	const u32 statsBytes = MESHLET_STATS_FIELDS * sizeof(u32);
	context.CopyBufferRegion(
		(phase == PHASE1_CULL) ? m_Phase1MeshletStatsReadback : m_Phase2MeshletStatsReadback,
		m_MeshletStatsBuffer,
		statsBytes,
		m_ReadbackIdx * statsBytes,
		0);
#endif // PROFILING_LEVEL >= 1
}

// =========================================================================
// Build HiZ pyramid from current depth attachment into m_pHiZTexture
// Uses AMD FidelityFX SPD (Single Pass Downsampler) for the entire mip chain.
// =========================================================================
void GBufferNode::BuildHiZ(render::CommandContext& context)
{
	using namespace render;

	auto pDepthAttachment = m_pRenderTargetPhase1->Attachment(eAttachmentPoint::DepthStencil);
	u32  hiZWidth    = m_pHiZTexture->Width();
	u32  hiZHeight   = m_pHiZTexture->Height();
	u32  hiZMipCount = m_pHiZTexture->MipLevels();
	u32  numGroupsX  = (hiZWidth  + 63) / 64;
	u32  numGroupsY  = (hiZHeight + 63) / 64;

	context.SetRenderPipeline(m_pHiZGenerationPSO.get());

	// --- Barriers: depth -> SRV, entire HiZ -> UAV, counter clear ---
	context.ClearBuffer(m_pSPDCounterBuffer, 0);
	context.TransitionBufferToWrite(m_pSPDCounterBuffer, ePipelineStage::ComputeShader, 0, true);

	context.TransitionBarrier(pDepthAttachment, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pHiZTexture, eTextureLayout::General);
	

	// --- Bind resources ---
	struct SPDPushConstants
	{
		u32 mip0Width;
		u32 mip0Height;
		u32 spdMipCount;
		u32 numWorkGroups;
	} constants = {
		.mip0Width     = hiZWidth,
		.mip0Height    = hiZHeight,
		.spdMipCount   = hiZMipCount - 1,
		.numWorkGroups = numGroupsX * numGroupsY,
	};
	context.SetComputeConstants(sizeof(constants), &constants);

	context.StageDescriptor("g_DepthSrc", pDepthAttachment, g_FrameData.pLinearClampMin);
	context.StageDescriptor("g_SPDCounter", m_pSPDCounterBuffer);

	u32 lastMip = hiZMipCount - 1;
	static constexpr u32 MAX_SPD_MIP_UAVS = 13; // g_MipUAV0..g_MipUAV12
	for (u32 i = 0; i < MAX_SPD_MIP_UAVS; ++i)
	{
		u32 mip = std::min(i, lastMip);
		context.StageDescriptorMip("g_MipUAV" + std::to_string(i), m_pHiZTexture, mip);
	}

	context.Dispatch(numGroupsX, numGroupsY, 1);

	context.TransitionBarrier(m_pHiZTexture, eTextureLayout::ShaderReadOnly, ALL_SUBRESOURCES, true);
}

// =========================================================================
// Main Apply: Two-Phase Occlusion Culling Pipeline
// =========================================================================
void GBufferNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(renderView);
	using namespace render;

	auto& rm = m_RenderDevice.GetResourceManager();
	auto& sr = rm.GetSceneResource();

	u32 numInstances     = sr.NumInstances();
	u32 numRequiredBits  = sr.NumMeshletVisibilitySlots();
	u32 numRequiredWords = (numRequiredBits + 31u) / 32u;
	if (numRequiredWords > m_NumMeshletVisibilityWords)
	{
		m_MeshletVisibilityBuffer->Resize(numRequiredWords * 2 * sizeof(u32), true);

		m_NumMeshletVisibilityWords = numRequiredWords * 2;
		m_bNeedsClear               = true;
	}

	if (m_bNeedsClear)
	{
		context.ClearBuffer(m_VisibilityBuffer, 0);
		context.ClearBuffer(m_MeshletVisibilityBuffer, 0);
		m_bNeedsClear = false;
	}

	g_FrameData.totalInstances = numInstances;
	if (m_ReadbackFrameCounter >= READBACK_SLOTS)
	{
		if (auto* p1 = static_cast<u32*>(m_Phase1CountReadback->MappedMemory()))
			g_FrameData.phase1InstanceDrawCount = p1[m_ReadbackIdx];
		if (auto* p2 = static_cast<u32*>(m_Phase2CountReadback->MappedMemory()))
			g_FrameData.phase2InstanceDrawCount = p2[m_ReadbackIdx];

#if PROFILING_LEVEL >= 1
		if (auto* p1m = static_cast<u32*>(m_Phase1MeshletStatsReadback->MappedMemory()))
		{
			const u32 base = m_ReadbackIdx * MESHLET_STATS_FIELDS;
			g_FrameData.phase1MeshletDrawn       = p1m[base + 0];
			g_FrameData.phase1MeshletTotal       = p1m[base + 1];
			g_FrameData.phase1TriangleCandidates = p1m[base + 2];
		}
		if (auto* p2m = static_cast<u32*>(m_Phase2MeshletStatsReadback->MappedMemory()))
		{
			const u32 base = m_ReadbackIdx * MESHLET_STATS_FIELDS;
			g_FrameData.phase2MeshletDrawn       = p2m[base + 0];
			g_FrameData.phase2MeshletTotal       = p2m[base + 1];
			g_FrameData.phase2TriangleCandidates = p2m[base + 2];
		}
#endif // PROFILING_LEVEL >= 1
	}

	{
		BAAMBOO_PROFILE_SCOPE(context,"Phase1Cull");
		DispatchCull(context, numInstances, PHASE1_CULL);

		context.CopyBufferRegion(m_Phase1CountReadback, m_DrawCountBuffer, sizeof(u32), m_ReadbackIdx * sizeof(u32), 0);
		context.TransitionBufferToRead(m_DrawCountBuffer, ePipelineStage::DrawIndirect, 0, true);
	}
	{
		BAAMBOO_PROFILE_SCOPE_STATS(context,"Phase1Draw");
		DrawGBuffer(context, m_pRenderTargetPhase1, numInstances, PHASE1_CULL);
		m_pRenderTargetPhase1->InvalidateImageLayout();
	}

	// ============================================================
	// BUILD INTERMEDIATE HiZ from Phase 1 depth
	// ============================================================
	{
		BAAMBOO_PROFILE_SCOPE(context,"BuildHiZ");
		BuildHiZ(context);
	}

	// ============================================================
	// PHASE 2: Test ALL instances (frustum + HZB).
	// Atomically set/clear each bit for next frame.
	// Only emit draws for newly visible (bit was 0 = not in Phase 1).
	// ============================================================
	{
		BAAMBOO_PROFILE_SCOPE(context,"Phase2Cull");
		DispatchCull(context, numInstances, PHASE2_CULL);

		context.CopyBufferRegion(m_Phase2CountReadback, m_DrawCountBuffer, sizeof(u32), m_ReadbackIdx * sizeof(u32), 0);
		context.TransitionBufferToRead(m_DrawCountBuffer, ePipelineStage::DrawIndirect, 0, true);
	}
	{
		BAAMBOO_PROFILE_SCOPE_STATS(context,"Phase2Draw");
		DrawGBuffer(context, m_pRenderTargetPhase2, numInstances, PHASE2_CULL);
		m_pRenderTargetPhase2->InvalidateImageLayout();
	}

	// Advance ring-buffer index for next frame.
	m_ReadbackIdx = (m_ReadbackIdx + 1) % READBACK_SLOTS;
	if (m_ReadbackFrameCounter < READBACK_SLOTS + 1)
		++m_ReadbackFrameCounter;

	// Export frame data for downstream passes
	g_FrameData.pColor    = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::Color0);
	g_FrameData.pGBuffer0 = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::Color0);
	g_FrameData.pGBuffer1 = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::Color1);
	g_FrameData.pGBuffer2 = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::Color2);
	g_FrameData.pGBuffer3 = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::Color3);
	g_FrameData.pDepth    = m_pRenderTargetPhase2->Attachment(eAttachmentPoint::DepthStencil);
}

void GBufferNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pRenderTargetPhase1)
		m_pRenderTargetPhase1->Resize(width, height, depth);
	if (m_pRenderTargetPhase2)
		m_pRenderTargetPhase2->Resize(width, height, depth);

	if (m_pHiZTexture)
		m_pHiZTexture->Resize(previousPow2(width), previousPow2(height), depth);
}

} // namespace baamboo
