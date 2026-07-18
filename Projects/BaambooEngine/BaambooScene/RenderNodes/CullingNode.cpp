#include "BaambooPch.h"
#include "CullingNode.h"

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

} // anonymous


CullingNode::CullingNode(render::RenderDevice& rd)
	: Super(rd, "CullingPass")
{
	using namespace render;

	// --- Mesh cull result buffers ---
	m_DrawIndexBuffer = Buffer::Create(rd, "CullingPass::DrawIndexBuffer",
		{
			.count              = kMaxEntityCount,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage,
		});
	m_DrawCountBuffer = Buffer::Create(rd, "CullingPass::DrawCountBuffer",
		{
			.count              = 1,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_Indirect | eBufferUsage_TransferSource | eBufferUsage_TransferDest | eBufferUsage_ShaderDeviceAddress,
		});
	m_CulledIndirectCommandBuffer = Buffer::Create(rd, "CullingPass::CulledIndirectCommandBuffer",
		{
			.count              = kMaxEntityCount,
			.elementSizeInBytes = sizeof(IndirectCommandData),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_Indirect,
		});
	m_VisibilityBuffer = Buffer::Create(rd, "CullingPass::VisibilityBuffer",
		{
			.count              = kMaxEntityCount,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	m_MeshletVisibilityBuffer = Buffer::Create(rd, "CullingPass::MeshletVisibilityBuffer",
		{
			.count              = kNumInitialMeshletVisibilityWords,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferSource | eBufferUsage_TransferDest,
		});
	m_NumMeshletVisibilityWords = kNumInitialMeshletVisibilityWords;

	m_pSPDCounterBuffer = Buffer::Create(rd, "CullingPass::SPDCounterBuffer",
		{
			.count              = 1,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	// --- Readback ring ---
	m_Phase1CountReadback = Buffer::Create(rd, "CullingPass::Phase1CountReadback",
		{
			.count              = kReadbackSlots,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
	m_Phase2CountReadback = Buffer::Create(rd, "CullingPass::Phase2CountReadback",
		{
			.count              = kReadbackSlots,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});

#if PROFILING_LEVEL >= 1
	m_MeshletStatsBuffer = Buffer::Create(rd, "CullingPass::MeshletStatsBuffer",
		{
			.count              = kMeshletStatsFields,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferSource | eBufferUsage_TransferDest,
		});
	m_Phase1MeshletStatsReadback = Buffer::Create(rd, "CullingPass::Phase1MeshletStatsReadback",
		{
			.count              = kReadbackSlots * kMeshletStatsFields,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
	m_Phase2MeshletStatsReadback = Buffer::Create(rd, "CullingPass::Phase2MeshletStatsReadback",
		{
			.count              = kReadbackSlots * kMeshletStatsFields,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
#endif // PROFILING_LEVEL >= 1

	// --- HiZ texture (previousPow2 of window so every SPD reduction is exactly 2x2) ---
	m_pHiZTexture = Texture::Create(rd, "CullingPass::HiZTexture",
		{
			.resolution    = { previousPow2(rd.WindowWidth()), previousPow2(rd.WindowHeight()), 1 },
			.format        = eFormat::R32_FLOAT,
			.imageUsage    = eTextureUsage_Sample | eTextureUsage_Storage,
			.bGenerateMips = true,
		});

	// --- Compute pipelines ---
	auto pCullingCS = Shader::Create(rd, "InstanceCullingCS", { .stage = eShaderStage::Compute, .filename = "InstanceCullingCS" });
	m_pInstanceCullingPSO = ComputePipeline::Create(rd, "InstanceCullingPSO");
	m_pInstanceCullingPSO->SetComputeShader(pCullingCS).Build();

	auto pHiZGenerationCS = Shader::Create(rd, "HiZGenerationCS", { .stage = eShaderStage::Compute, .filename = "HiZGenerationCS" });
	m_pHiZGenerationPSO = ComputePipeline::Create(rd, "HiZGenerationPSO");
	m_pHiZGenerationPSO->SetComputeShader(pHiZGenerationCS).Build();

	auto pVoxelPatchCS = Shader::Create(rd, "VoxelMeshDataPatchCS", { .stage = eShaderStage::Compute, .filename = "VoxelMeshDataPatchCS" });
	m_pVoxelMeshDataPatchPSO = ComputePipeline::Create(rd, "VoxelMeshDataPatchPSO");
	m_pVoxelMeshDataPatchPSO->SetComputeShader(pVoxelPatchCS).Build();

	g_FrameData.pHiZ = m_pHiZTexture;
}

CullingNode::~CullingNode()
{
}

// =========================================================================
// Dispatch instance culling compute shader
// =========================================================================
void CullingNode::DispatchMeshCull(render::CommandContext& context, u32 numInstances, u32 phase)
{
	using namespace render;

	context.ClearBuffer(m_DrawCountBuffer, 0);

	if (numInstances == 0)
		return;

	context.SetRenderPipeline(m_pInstanceCullingPSO.get());

	context.TransitionBufferToWrite(m_CulledIndirectCommandBuffer, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_DrawCountBuffer, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_DrawIndexBuffer, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_VisibilityBuffer, ePipelineStage::ComputeShader);
	context.TransitionTextureToRead(m_pHiZTexture, ePipelineStage::ComputeShader);

	struct
	{
		u32 numInstances;
		u32 cullingPhase;
	} constant = {
		.numInstances = numInstances,
		.cullingPhase = phase,
	};
	context.SetComputeConstants(sizeof(constant), &constant);

	context.StageDescriptor("g_IndirectCommands", m_CulledIndirectCommandBuffer);
	context.StageDescriptor("g_DrawIDs", m_DrawIndexBuffer);
	context.StageDescriptor("g_DrawCount", m_DrawCountBuffer);
	context.StageDescriptor("g_VisibilityBuffer", m_VisibilityBuffer);
	context.StageDescriptor("g_HiZTexture", m_pHiZTexture, g_FrameData.pLinearClampMin);

	context.Dispatch1D< 64 >(numInstances);

	context.TransitionBufferToRead(m_CulledIndirectCommandBuffer, ePipelineStage::DrawIndirect);
	context.TransitionBufferToIndirectArgs(m_DrawCountBuffer, 0, false);
	context.TransitionBufferToRead(m_DrawIndexBuffer, ePipelineStage::TaskShader, 0, true);
}

void CullingNode::PatchVoxelMeshData(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	if (!m_pVoxelMeshDataPatchPSO || !renderView.voxelTerrain.bValid)
		return; // no voxel chunk authored this frame

	auto& rm = m_RenderDevice.GetResourceManager();
	auto& sr = rm.GetSceneResource();

	auto pVoxelCounts = g_FrameData.pVoxelChunkCounts.lock();
	auto pMeshData    = sr.GetMeshDataBuffer();
	if (!pVoxelCounts || !pMeshData)
		return;

	context.SetRenderPipeline(m_pVoxelMeshDataPatchPSO.get());

	context.TransitionBufferToRead(pVoxelCounts, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(pMeshData, ePipelineStage::ComputeShader);

	// The voxel MeshData was appended after the static meshes, so its index == static mesh count.
	struct
	{
		u32 voxelMeshID;
	} constant = { (u32)renderView.meshes.size() };
	context.SetComputeConstants(sizeof(constant), &constant);

	context.StageDescriptor("g_VoxelCounts", pVoxelCounts);
	context.StageDescriptor("g_MeshData", pMeshData);

	context.Dispatch(1, 1, 1);

	context.TransitionBufferToRead(pMeshData, ePipelineStage::AllShader);
}

// =========================================================================
// Build HiZ pyramid from current depth attachment
// =========================================================================
void CullingNode::BuildHiZ(render::CommandContext& context)
{
	using namespace render;

	if (!m_pGBufferNode)
		return;

	auto pDepthAttachment = m_pGBufferNode->GetDepthAttachment();
	if (!pDepthAttachment)
		return;

	u32 hiZWidth    = m_pHiZTexture->Width();
	u32 hiZHeight   = m_pHiZTexture->Height();
	u32 hiZMipCount = m_pHiZTexture->MipLevels();
	u32 numGroupsX  = (hiZWidth  + 63) / 64;
	u32 numGroupsY  = (hiZHeight + 63) / 64;

	context.SetRenderPipeline(m_pHiZGenerationPSO.get());

	context.ClearBuffer(m_pSPDCounterBuffer, 0);
	context.TransitionBufferToWrite(m_pSPDCounterBuffer, ePipelineStage::ComputeShader, 0, true);

	context.TransitionTextureToRead(pDepthAttachment, ePipelineStage::ComputeShader);
	context.TransitionTextureToWrite(m_pHiZTexture, ePipelineStage::ComputeShader);

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
	static constexpr u32 kMaxSpdMipUavs = 13; // g_MipUAV0..g_MipUAV12
	for (u32 i = 0; i < kMaxSpdMipUavs; ++i)
	{
		u32 mip = std::min(i, lastMip);
		context.StageDescriptorMip("g_MipUAV" + std::to_string(i), m_pHiZTexture, mip);
	}

	context.Dispatch(numGroupsX, numGroupsY, 1);

	context.TransitionTextureToRead(m_pHiZTexture, ePipelineStage::ComputeShader | ePipelineStage::TaskShader, ALL_SUBRESOURCES, true);
}

// =========================================================================
// Resize MeshletVisibility buffer when scene meshlet count grows.
// =========================================================================
void CullingNode::EnsureMeshletVisibility(u32 numRequiredWords)
{
	if (numRequiredWords > m_NumMeshletVisibilityWords)
	{
		m_MeshletVisibilityBuffer->Resize(numRequiredWords * 2 * sizeof(u32), true);
		m_NumMeshletVisibilityWords = numRequiredWords * 2;
		m_bNeedsClear               = true;
	}
}

// =========================================================================
// Publish readback stats
// =========================================================================
void CullingNode::PublishReadbackStats()
{
	if (m_ReadbackFrameCounter < kReadbackSlots)
		return;

	const u64 countOffset = m_ReadbackIdx * sizeof(u32);
	m_Phase1CountReadback->InvalidateMappedRange(countOffset, sizeof(u32));
	m_Phase2CountReadback->InvalidateMappedRange(countOffset, sizeof(u32));

	if (auto* p1 = static_cast< u32* >(m_Phase1CountReadback->MappedMemory()))
		g_FrameData.phase1InstanceDrawCount = p1[m_ReadbackIdx];
	if (auto* p2 = static_cast< u32* >(m_Phase2CountReadback->MappedMemory()))
		g_FrameData.phase2InstanceDrawCount = p2[m_ReadbackIdx];

#if PROFILING_LEVEL >= 1
	const u64 statsOffset = m_ReadbackIdx * kMeshletStatsFields * sizeof(u32);
	const u64 statsSize   = kMeshletStatsFields * sizeof(u32);
	m_Phase1MeshletStatsReadback->InvalidateMappedRange(statsOffset, statsSize);
	m_Phase2MeshletStatsReadback->InvalidateMappedRange(statsOffset, statsSize);

	if (auto* p1m = static_cast< u32* >(m_Phase1MeshletStatsReadback->MappedMemory()))
	{
		const u32 base = m_ReadbackIdx * kMeshletStatsFields;
		g_FrameData.phase1MeshletDrawn       = p1m[base + 0];
		g_FrameData.phase1MeshletTotal       = p1m[base + 1];
		g_FrameData.phase1TriangleCandidates = p1m[base + 2];
	}
	if (auto* p2m = static_cast< u32* >(m_Phase2MeshletStatsReadback->MappedMemory()))
	{
		const u32 base = m_ReadbackIdx * kMeshletStatsFields;
		g_FrameData.phase2MeshletDrawn       = p2m[base + 0];
		g_FrameData.phase2MeshletTotal       = p2m[base + 1];
		g_FrameData.phase2TriangleCandidates = p2m[base + 2];
	}
#endif // PROFILING_LEVEL >= 1
}

MeshCullOutputs CullingNode::MakeMeshCullOutputs(u32 numInstances, u32 phase) const
{
	MeshCullOutputs outputs;
	outputs.pIndirectCommands  = m_CulledIndirectCommandBuffer;
	outputs.pDrawCount         = m_DrawCountBuffer;
	outputs.pDrawIndex         = m_DrawIndexBuffer;
	outputs.pMeshletVisibility = m_MeshletVisibilityBuffer;
#if PROFILING_LEVEL >= 1
	outputs.pMeshletStats      = m_MeshletStatsBuffer;
#endif
	outputs.pHiZ               = m_pHiZTexture;
	outputs.numInstances       = numInstances;
	outputs.phase              = phase;
	return outputs;
}

// =========================================================================
// Apply: Two-Phase Occlusion Culling Pipeline
// =========================================================================
void CullingNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	auto& rm = m_RenderDevice.GetResourceManager();
	auto& sr = rm.GetSceneResource();

	u32 numInstances     = sr.NumInstances();
	u32 numRequiredBits  = sr.NumMeshletVisibilitySlots();
	u32 numRequiredWords = (numRequiredBits + 31u) / 32u;

	EnsureMeshletVisibility(numRequiredWords);

	if (m_bNeedsClear)
	{
		context.ClearBuffer(m_VisibilityBuffer, 0);
		context.ClearBuffer(m_MeshletVisibilityBuffer, 0);
		m_bNeedsClear = false;
	}

	g_FrameData.totalInstances = numInstances;
	PublishReadbackStats();

	// ============================================================
	// VOXEL: build chunk geometry, then patch its GPU meshlet count into the unified MeshData.
	// ============================================================
	if (m_pVoxelNode)
		m_pVoxelNode->BuildChunkGeometryIfNeeded(context, renderView);
	PatchVoxelMeshData(context, renderView);

	// ============================================================
	// PHASE 1
	// ============================================================
	{
		BAAMBOO_PROFILE_SCOPE(context, "Phase1Cull");
		DispatchMeshCull(context, numInstances, kPhase1Cull);

		context.CopyBufferRegion(m_Phase1CountReadback, m_DrawCountBuffer, sizeof(u32), m_ReadbackIdx * sizeof(u32), 0);
		context.TransitionBufferToRead(m_DrawCountBuffer, ePipelineStage::DrawIndirect, 0, true);
	}
	{
		BAAMBOO_PROFILE_SCOPE_STATS(context, "Phase1Draw");
		if (m_pGBufferNode)
			m_pGBufferNode->DrawGBufferPhase1(context, MakeMeshCullOutputs(numInstances, kPhase1Cull));

#if PROFILING_LEVEL >= 1
		const u32 statsBytes = kMeshletStatsFields * sizeof(u32);
		context.CopyBufferRegion(m_Phase1MeshletStatsReadback, m_MeshletStatsBuffer, statsBytes, m_ReadbackIdx * statsBytes, 0);
#endif
	}

	// ============================================================
	// BUILD HiZ from Phase 1 depth
	// ============================================================
	{
		BAAMBOO_PROFILE_SCOPE(context, "BuildHiZ");
		BuildHiZ(context);
	}

	// ============================================================
	// PHASE 2
	// ============================================================
	{
		BAAMBOO_PROFILE_SCOPE(context, "Phase2Cull");
		DispatchMeshCull(context, numInstances, kPhase2Cull);

		context.CopyBufferRegion(m_Phase2CountReadback, m_DrawCountBuffer, sizeof(u32), m_ReadbackIdx * sizeof(u32), 0);
		context.TransitionBufferToRead(m_DrawCountBuffer, ePipelineStage::DrawIndirect, 0, true);
	}
	{
		BAAMBOO_PROFILE_SCOPE_STATS(context, "Phase2Draw");
		if (m_pGBufferNode)
			m_pGBufferNode->DrawGBufferPhase2(context, MakeMeshCullOutputs(numInstances, kPhase2Cull));

#if PROFILING_LEVEL >= 1
		const u32 statsBytes = kMeshletStatsFields * sizeof(u32);
		context.CopyBufferRegion(m_Phase2MeshletStatsReadback, m_MeshletStatsBuffer, statsBytes, m_ReadbackIdx * statsBytes, 0);
#endif
	}

	// Advance ring-buffer index for next frame.
	m_ReadbackIdx = (m_ReadbackIdx + 1) % kReadbackSlots;
	if (m_ReadbackFrameCounter < kReadbackSlots + 1)
		++m_ReadbackFrameCounter;
}

void CullingNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pHiZTexture)
	{
		m_pHiZTexture->Resize(previousPow2(width), previousPow2(height), depth);
		g_FrameData.pHiZ = m_pHiZTexture; // re-publish (Arc same, but dims changed — Weak still valid)
	}
	if (m_pGBufferNode)
		m_pGBufferNode->Resize(width, height, depth);
	if (m_pVoxelNode)
		m_pVoxelNode->Resize(width, height, depth);
}


} // namespace baamboo
