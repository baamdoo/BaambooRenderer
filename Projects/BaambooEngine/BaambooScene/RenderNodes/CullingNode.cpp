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
			.count              = MAX_ENTITY_COUNT,
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
			.count              = MAX_ENTITY_COUNT,
			.elementSizeInBytes = sizeof(IndirectCommandData),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_Indirect,
		});
	m_VisibilityBuffer = Buffer::Create(rd, "CullingPass::VisibilityBuffer",
		{
			.count              = MAX_ENTITY_COUNT,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	m_MeshletVisibilityBuffer = Buffer::Create(rd, "CullingPass::MeshletVisibilityBuffer",
		{
			.count              = NUM_INITIAL_MESHLET_VISIBILITY_WORDS,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferSource | eBufferUsage_TransferDest,
		});
	m_NumMeshletVisibilityWords = NUM_INITIAL_MESHLET_VISIBILITY_WORDS;

	m_pSPDCounterBuffer = Buffer::Create(rd, "CullingPass::SPDCounterBuffer",
		{
			.count              = 1,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	// --- Readback ring ---
	m_Phase1CountReadback = Buffer::Create(rd, "CullingPass::Phase1CountReadback",
		{
			.count              = READBACK_SLOTS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
	m_Phase2CountReadback = Buffer::Create(rd, "CullingPass::Phase2CountReadback",
		{
			.count              = READBACK_SLOTS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});

#if PROFILING_LEVEL >= 1
	m_MeshletStatsBuffer = Buffer::Create(rd, "CullingPass::MeshletStatsBuffer",
		{
			.count              = MESHLET_STATS_FIELDS,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferSource | eBufferUsage_TransferDest,
		});
	m_Phase1MeshletStatsReadback = Buffer::Create(rd, "CullingPass::Phase1MeshletStatsReadback",
		{
			.count              = READBACK_SLOTS * MESHLET_STATS_FIELDS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
	m_Phase2MeshletStatsReadback = Buffer::Create(rd, "CullingPass::Phase2MeshletStatsReadback",
		{
			.count              = READBACK_SLOTS * MESHLET_STATS_FIELDS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
#endif // PROFILING_LEVEL >= 1

	// --- Terrain cull count readback ring (mirrors mesh Phase1/Phase2 pattern) ---
	m_TerrainPhase1CountReadback = Buffer::Create(rd, "CullingPass::TerrainPhase1CountReadback",
		{
			.count              = READBACK_SLOTS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
	m_TerrainPhase2CountReadback = Buffer::Create(rd, "CullingPass::TerrainPhase2CountReadback",
		{
			.count              = READBACK_SLOTS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
#if PROFILING_LEVEL >= 1
	m_TerrainPhase1LodStatsReadback = Buffer::Create(rd, "CullingPass::TerrainPhase1LodStatsReadback",
		{
			.count              = READBACK_SLOTS * TERRAIN_LOD_STATS_DEPTHS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});
#endif
	m_TerrainPhase2LodStatsReadback = Buffer::Create(rd, "CullingPass::TerrainPhase2LodStatsReadback",
		{
			.count              = READBACK_SLOTS * TERRAIN_LOD_STATS_DEPTHS,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
		});

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
	context.TransitionBarrier(m_pHiZTexture, eTextureLayout::ShaderReadOnly);

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
	context.TransitionBufferToRead(m_DrawCountBuffer, ePipelineStage::DrawIndirect);
	context.TransitionBufferToRead(m_DrawIndexBuffer, ePipelineStage::TaskShader, 0, true);
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

	context.TransitionBarrier(pDepthAttachment, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pHiZTexture, eTextureLayout::General);

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
	if (m_ReadbackFrameCounter < READBACK_SLOTS)
		return;

	if (auto* p1 = static_cast< u32* >(m_Phase1CountReadback->MappedMemory()))
		g_FrameData.phase1InstanceDrawCount = p1[m_ReadbackIdx];
	if (auto* p2 = static_cast< u32* >(m_Phase2CountReadback->MappedMemory()))
		g_FrameData.phase2InstanceDrawCount = p2[m_ReadbackIdx];

#if PROFILING_LEVEL >= 1
	if (auto* p1m = static_cast< u32* >(m_Phase1MeshletStatsReadback->MappedMemory()))
	{
		const u32 base = m_ReadbackIdx * MESHLET_STATS_FIELDS;
		g_FrameData.phase1MeshletDrawn       = p1m[base + 0];
		g_FrameData.phase1MeshletTotal       = p1m[base + 1];
		g_FrameData.phase1TriangleCandidates = p1m[base + 2];
	}
	if (auto* p2m = static_cast< u32* >(m_Phase2MeshletStatsReadback->MappedMemory()))
	{
		const u32 base = m_ReadbackIdx * MESHLET_STATS_FIELDS;
		g_FrameData.phase2MeshletDrawn       = p2m[base + 0];
		g_FrameData.phase2MeshletTotal       = p2m[base + 1];
		g_FrameData.phase2TriangleCandidates = p2m[base + 2];
	}
#endif // PROFILING_LEVEL >= 1

	// --- Terrain cull stats ---
	if (m_pTerrainNode)
	{
		g_FrameData.terrainTotalPatches = m_pTerrainNode->GetTotalNodeCount();
		const u32 perPatchTris          = m_pTerrainNode->GetPerPatchTriangles();

		if (auto* t1 = static_cast< u32* >(m_TerrainPhase1CountReadback->MappedMemory()))
		{
			const u32 cnt = t1[m_ReadbackIdx];
			g_FrameData.terrainPhase1DrawCount = cnt;
			g_FrameData.terrainPhase1Triangles = cnt * perPatchTris;
		}
		if (auto* t2 = static_cast< u32* >(m_TerrainPhase2CountReadback->MappedMemory()))
		{
			const u32 cnt = t2[m_ReadbackIdx];
			g_FrameData.terrainPhase2DrawCount = cnt;
			g_FrameData.terrainPhase2Triangles = cnt * perPatchTris;
		}
#if PROFILING_LEVEL >= 1
		if (auto* t1lod = static_cast< u32* >(m_TerrainPhase1LodStatsReadback->MappedMemory()))
		{
			const u32 base = m_ReadbackIdx * TERRAIN_LOD_STATS_DEPTHS;
			for (u32 i = 0u; i < TERRAIN_LOD_STATS_DEPTHS; ++i)
				g_FrameData.terrainPhase1LodPatches[i] = t1lod[base + i];
		}
		if (auto* t2lod = static_cast< u32* >(m_TerrainPhase2LodStatsReadback->MappedMemory()))
		{
			const u32 base = m_ReadbackIdx * TERRAIN_LOD_STATS_DEPTHS;
			for (u32 i = 0u; i < TERRAIN_LOD_STATS_DEPTHS; ++i)
				g_FrameData.terrainPhase2LodPatches[i] = t2lod[base + i];
		}
#endif
	}
	else
	{
		g_FrameData.terrainTotalPatches    = 0u;
		g_FrameData.terrainPhase1DrawCount = 0u;
		g_FrameData.terrainPhase2DrawCount = 0u;
		g_FrameData.terrainPhase1Triangles = 0u;
		g_FrameData.terrainPhase2Triangles = 0u;
#if PROFILING_LEVEL >= 1
		for (u32 i = 0u; i < TERRAIN_LOD_STATS_DEPTHS; ++i)
		{
			g_FrameData.terrainPhase1LodPatches[i] = 0u;
			g_FrameData.terrainPhase2LodPatches[i] = 0u;
		}
#endif
	}
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
	UNUSED(renderView);
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
	// PHASE 1
	// ============================================================
	{
		BAAMBOO_PROFILE_SCOPE(context, "Phase1Cull");
		DispatchMeshCull(context, numInstances, PHASE1_CULL);
		if (m_pTerrainNode)
			m_pTerrainNode->DispatchTerrainCull(context, CullingNode::PHASE1_CULL, m_pHiZTexture, renderView);

		context.CopyBufferRegion(m_Phase1CountReadback, m_DrawCountBuffer, sizeof(u32), m_ReadbackIdx * sizeof(u32), 0);
		context.TransitionBufferToRead(m_DrawCountBuffer, ePipelineStage::DrawIndirect, 0, true);

		if (m_pTerrainNode)
		{
			const auto& pTerrainCount = m_pTerrainNode->GetDrawCountBuffer();
			if (pTerrainCount)
				context.CopyBufferRegion(m_TerrainPhase1CountReadback, pTerrainCount, sizeof(u32), m_ReadbackIdx * sizeof(u32), 0);
#if PROFILING_LEVEL >= 1
			const auto& pTerrainLodStats = m_pTerrainNode->GetLodStatsBuffer();
			if (pTerrainLodStats)
				context.CopyBufferRegion(m_TerrainPhase1LodStatsReadback, pTerrainLodStats,
					TERRAIN_LOD_STATS_DEPTHS * sizeof(u32),
					m_ReadbackIdx * TERRAIN_LOD_STATS_DEPTHS * sizeof(u32), 0);
#endif
		}
	}
	{
		BAAMBOO_PROFILE_SCOPE_STATS(context, "Phase1Draw");
		if (m_pGBufferNode)
			m_pGBufferNode->DrawGBufferPhase1(context, MakeMeshCullOutputs(numInstances, PHASE1_CULL));
		if (m_pTerrainNode && m_pGBufferNode)
			m_pTerrainNode->DrawTerrainPhase1(context, m_pGBufferNode->GetPhase2RenderTarget(), renderView);

#if PROFILING_LEVEL >= 1
		const u32 statsBytes = MESHLET_STATS_FIELDS * sizeof(u32);
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
		DispatchMeshCull(context, numInstances, PHASE2_CULL);
		if (m_pTerrainNode)
			m_pTerrainNode->DispatchTerrainCull(context, CullingNode::PHASE2_CULL, m_pHiZTexture, renderView);

		context.CopyBufferRegion(m_Phase2CountReadback, m_DrawCountBuffer, sizeof(u32), m_ReadbackIdx * sizeof(u32), 0);
		context.TransitionBufferToRead(m_DrawCountBuffer, ePipelineStage::DrawIndirect, 0, true);

		if (m_pTerrainNode)
		{
			const auto& pTerrainCount = m_pTerrainNode->GetDrawCountBuffer();
			if (pTerrainCount)
				context.CopyBufferRegion(m_TerrainPhase2CountReadback, pTerrainCount, sizeof(u32), m_ReadbackIdx * sizeof(u32), 0);
#if PROFILING_LEVEL >= 1
			const auto& pTerrainLodStats = m_pTerrainNode->GetLodStatsBuffer();
			if (pTerrainLodStats)
				context.CopyBufferRegion(m_TerrainPhase2LodStatsReadback, pTerrainLodStats,
					TERRAIN_LOD_STATS_DEPTHS * sizeof(u32),
					m_ReadbackIdx * TERRAIN_LOD_STATS_DEPTHS * sizeof(u32), 0);
#endif
		}
	}
	{
		BAAMBOO_PROFILE_SCOPE_STATS(context, "Phase2Draw");
		if (m_pGBufferNode)
			m_pGBufferNode->DrawGBufferPhase2(context, MakeMeshCullOutputs(numInstances, PHASE2_CULL));
		if (m_pTerrainNode && m_pGBufferNode)
			m_pTerrainNode->DrawTerrainPhase2(context, m_pGBufferNode->GetPhase2RenderTarget(), renderView);

#if PROFILING_LEVEL >= 1
		const u32 statsBytes = MESHLET_STATS_FIELDS * sizeof(u32);
		context.CopyBufferRegion(m_Phase2MeshletStatsReadback, m_MeshletStatsBuffer, statsBytes, m_ReadbackIdx * statsBytes, 0);
#endif
	}

	// Advance ring-buffer index for next frame.
	m_ReadbackIdx = (m_ReadbackIdx + 1) % READBACK_SLOTS;
	if (m_ReadbackFrameCounter < READBACK_SLOTS + 1)
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
	if (m_pTerrainNode)
		m_pTerrainNode->Resize(width, height, depth); // currently no-op, kept for symmetry
}


} // namespace baamboo
