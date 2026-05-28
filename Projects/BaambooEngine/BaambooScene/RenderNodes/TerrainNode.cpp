#include "BaambooPch.h"
#include "TerrainNode.h"
#include "GBufferNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"

#include "BaambooScene/Scene.h"

namespace baamboo
{

namespace
{
	constexpr float FORCE_FINEST_LOD_RANGE_METER = 1.0e20f;
}

TerrainNode::TerrainNode(render::RenderDevice& rd)
	: Super(rd, "TerrainPass")
{
	using namespace render;

	m_pQuadtreeNodesBuffer = Buffer::Create(rd, "TerrainPass::QuadtreeNodes",
		{
			.count              = MAX_QUADTREE_NODES,
			.elementSizeInBytes = sizeof(TerrainNodeGPU),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	m_pCulledPatches = Buffer::Create(rd, "TerrainPass::CulledPatches",
		{
			.count              = MAX_CULLED_PATCHES,
			.elementSizeInBytes = sizeof(PatchInstance),
			.bufferUsage        = eBufferUsage_Storage, // UAV (cull CS writes) + SRV (TerrainMS reads)
		});

	m_pIndirectCommands = Buffer::Create(rd, "TerrainPass::IndirectCommands",
		{
			.count              = MAX_CULLED_PATCHES,
			.elementSizeInBytes = sizeof(IndirectCommandData),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_Indirect,
		});

	m_pDrawCountBuffer = Buffer::Create(rd, "TerrainPass::DrawCount",
		{
			.count              = 1,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_Indirect
			                    | eBufferUsage_TransferSource | eBufferUsage_TransferDest
			                    | eBufferUsage_ShaderDeviceAddress,
		});

#if PROFILING_LEVEL >= 1
	m_pLodStatsBuffer = Buffer::Create(rd, "TerrainPass::LodStats",
		{
			.count              = TERRAIN_MAX_LOD_DEPTHS,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferSource | eBufferUsage_TransferDest,
		});
#endif

	m_pNodeVisibilityBuffer = Buffer::Create(rd, "TerrainPass::NodeVisibility",
		{
			.count              = MAX_QUADTREE_NODES,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	auto pCullingCS = Shader::Create(rd, "TerrainPatchCullingCS",
		{ .stage = eShaderStage::Compute, .filename = "TerrainPatchCullingCS" });
	m_pCullingPSO = ComputePipeline::Create(rd, "TerrainPatchCullingPSO");
	m_pCullingPSO->SetComputeShader(pCullingCS).Build();
}

void TerrainNode::SetGBufferNode(const Arc< GBufferNode >& pNode)
{
	using namespace render;

	m_pGBufferNode = pNode;
	if (!m_pGBufferNode)
		return;

	auto pSharedRT = m_pGBufferNode->GetPhase2RenderTarget();
	if (!pSharedRT)
		return;

	auto pMS = Shader::Create(m_RenderDevice, "TerrainMS", { .stage = eShaderStage::Mesh,     .filename = "TerrainMS" });
	auto pPS = Shader::Create(m_RenderDevice, "TerrainPS", { .stage = eShaderStage::Fragment, .filename = "TerrainPS" });

	m_pTerrainPSO = GraphicsPipeline::Create(m_RenderDevice, "TerrainPSO");
	m_pTerrainPSO->SetMeshShaders(pMS, pPS)
	             .SetRenderTarget(pSharedRT)
	             .SetCullMode(eCullMode::None) // skirts + quad-patch
	             .SetDepthWriteEnable(true, eCompareOp::Greater).Build(); // reversed-Z
}

void TerrainNode::SetQuadtreeConfig(const QuadtreeConfig& cfg)
{
	m_Config = cfg;
	m_bConfigDirty = true;
	m_bQuadtreeUploaded = false;
	m_bVisibilityNeedsClear = true;
}

void TerrainNode::BuildQuadtree()
{
	TerrainQuadtree::Config qc{};
	qc.rootOriginXZ  = float2(m_Config.rootOriginX, m_Config.rootOriginZ);
	qc.rootSizeMeter = m_Config.rootSizeMeter;
	qc.terrainMinY   = m_Config.heightMinMeter;
	qc.terrainMaxY   = m_Config.heightMinMeter + m_Config.heightRangeMeter;
	qc.lodRangeBase  = m_Config.bForceFinestLOD ? FORCE_FINEST_LOD_RANGE_METER : m_Config.lodRangeBaseMeter;
	qc.lodMorphK     = m_Config.bForceFinestLOD ? 1.0f : m_Config.lodMorphK;
	qc.maxDepth      = std::min(m_Config.maxDepth, TERRAIN_MAX_LOD_DEPTHS - 1u);
	qc.gridN         = m_Config.gridN;
	m_Quadtree.Build(qc);
}

void TerrainNode::FillTerrainParams(TerrainParams& params) const
{
	std::memset(&params, 0, sizeof(TerrainParams));

	const u32   rho        = m_pHeightmap ? m_pHeightmap->Width() : 1u;
	const float invRes     = 1.0f / static_cast< float >(rho);

	params.terrainOriginX   = m_Config.rootOriginX;
	params.terrainOriginZ   = m_Config.rootOriginZ;
	params.terrainSizeMeter = m_Config.terrainSizeMeter;
	params.gridN            = m_Config.gridN;

	params.heightMinMeter   = m_Config.heightMinMeter;
	params.heightRangeMeter = m_Config.heightRangeMeter;
	params.heightmapTexel   = invRes;
	params.worldPerTexel    = m_Config.terrainSizeMeter * invRes;

	params.heightmapRes = rho;
	params.maxDepth     = m_Quadtree.GetConfig().maxDepth;
	params.numPatches   = 0u;

	const auto& rs = m_Quadtree.RangeStarts();
	const auto& re = m_Quadtree.RangeEnds();
	for (u32 d = 0u; d < TERRAIN_MAX_LOD_DEPTHS; ++d)
	{
		params.lodRangeStart[d] = (d < rs.size()) ? rs[d] : 0.0f;
		params.lodRangeEnd  [d] = (d < re.size()) ? re[d] : 0.0f;
	}
}

void TerrainNode::EnsureHeightmap()
{
	using namespace render;

	if (m_pHeightmap && m_Config.heightmapPath == m_HeightmapPathCache)
		return;

	m_HeightmapPathCache = m_Config.heightmapPath;

	auto& rm = m_RenderDevice.GetResourceManager();
	if (!m_Config.heightmapPath.empty() && fs::exists(m_Config.heightmapPath))
	{
		m_pHeightmap = rm.LoadTexture(m_Config.heightmapPath);
	}
	else
	{
		fprintf(stderr,
			"[TerrainNode] heightmap not found: \"%s\" — flat placeholder. "
			"Export a Gaea R16_UNORM .dds there (Docs/Terrain/01 Step 7).\n",
			m_Config.heightmapPath.c_str());
		m_pHeightmap = rm.GetFlatBlackTexture();
	}
	BB_ASSERT(m_pHeightmap != nullptr, "TerrainNode: heightmap is null");
}

void TerrainNode::EnsureQuadtreeUpload(render::CommandContext& context)
{
	if (m_bQuadtreeUploaded)
		return;

	const auto& gpuNodes = m_Quadtree.GetGPUNodes();
	const u32   nodeCount = static_cast< u32 >(gpuNodes.size());
	if (nodeCount == 0u)
		return;

	BB_ASSERT(nodeCount <= MAX_QUADTREE_NODES,
		"TerrainNode: quadtree node count exceeds MAX_QUADTREE_NODES; bump the constant");

	context.UploadData(m_pQuadtreeNodesBuffer, gpuNodes.data(), nodeCount, sizeof(TerrainNodeGPU));
	m_bQuadtreeUploaded = true;
}

void TerrainNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(context);
	UNUSED(renderView);
}

void TerrainNode::DispatchTerrainCull(render::CommandContext&    context,
                                       u32                        phase,
                                       const Arc< render::Texture >& pHiZTexture,
                                       const SceneRenderView&     renderView)
{
	UNUSED(renderView);
	using namespace render;

	if (!m_pCullingPSO)
		return;

	// --- Lazy heightmap + quadtree setup ---
	EnsureHeightmap();
	if (m_bConfigDirty)
	{
		BuildQuadtree();
		m_bConfigDirty = false;
	}
	EnsureQuadtreeUpload(context);

	const u32 numNodes = m_Quadtree.NumNodes();
	if (numNodes == 0u)
		return;

	if (m_bVisibilityNeedsClear)
	{
		context.ClearBuffer(m_pNodeVisibilityBuffer, 0);
		m_bVisibilityNeedsClear = false;
	}

	context.ClearBuffer(m_pDrawCountBuffer, 0);
#if PROFILING_LEVEL >= 1
	context.ClearBuffer(m_pLodStatsBuffer, 0);
#endif

	context.SetRenderPipeline(m_pCullingPSO.get());

	context.TransitionBufferToRead (m_pQuadtreeNodesBuffer,   ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pCulledPatches,         ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pIndirectCommands,      ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pDrawCountBuffer,       ePipelineStage::ComputeShader);
#if PROFILING_LEVEL >= 1
	context.TransitionBufferToWrite(m_pLodStatsBuffer,        ePipelineStage::ComputeShader);
#endif
	context.TransitionBufferToWrite(m_pNodeVisibilityBuffer,  ePipelineStage::ComputeShader);
	if (pHiZTexture)
		context.TransitionBarrier(pHiZTexture, eTextureLayout::ShaderReadOnly);

	struct
	{
		u32   numNodes;
		u32   cullingPhase;
		float lodRangeBase;
		float lodMorphK;
		u32   maxDepth;
	} constants = {
		.numNodes     = numNodes,
		.cullingPhase = phase,
		.lodRangeBase = m_Quadtree.GetConfig().lodRangeBase,
		.lodMorphK    = m_Quadtree.GetConfig().lodMorphK,
		.maxDepth     = m_Quadtree.GetConfig().maxDepth,
	};
	context.SetComputeConstants(sizeof(constants), &constants);

	context.StageDescriptor("g_TerrainNodes",     m_pQuadtreeNodesBuffer);
	context.StageDescriptor("g_CulledPatches",    m_pCulledPatches);
	context.StageDescriptor("g_IndirectCommands", m_pIndirectCommands);
	context.StageDescriptor("g_DrawCount",        m_pDrawCountBuffer);
	context.StageDescriptor("g_NodeVisibility",   m_pNodeVisibilityBuffer);
#if PROFILING_LEVEL >= 1
	context.StageDescriptor("g_LodStats",         m_pLodStatsBuffer);
#endif
	if (pHiZTexture)
		context.StageDescriptor("g_HiZTexture", pHiZTexture, g_FrameData.pLinearClampMin);

	context.Dispatch1D< 64 >(numNodes);

	context.TransitionBufferToRead(m_pIndirectCommands, ePipelineStage::DrawIndirect);
	context.TransitionBufferToRead(m_pDrawCountBuffer,  ePipelineStage::DrawIndirect);
#if PROFILING_LEVEL >= 1
	context.TransitionBufferToRead(m_pLodStatsBuffer,   ePipelineStage::Copy);
#endif
	context.TransitionBufferToRead(m_pCulledPatches,    ePipelineStage::MeshShader);
}

void TerrainNode::DrawTerrainPhase1(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView)
{
	DrawTerrainImpl(context, rt, renderView);
}

void TerrainNode::DrawTerrainPhase2(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView)
{
	DrawTerrainImpl(context, rt, renderView);
}

void TerrainNode::DrawTerrainImpl(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView)
{
	using namespace render;
	UNUSED(renderView);

	if (!m_pTerrainPSO || !rt || !m_pHeightmap)
		return;

	context.TransitionBarrier(m_pHeightmap, eTextureLayout::ShaderReadOnly);

	TerrainParams params{};
	FillTerrainParams(params);

	context.BeginRenderPass(rt);
	{
		context.SetRenderPipeline(m_pTerrainPSO.get());

		context.SetGraphicsDynamicUniformBuffer("g_Terrain", sizeof(TerrainParams), &params);

		context.StageDescriptor("g_Heightmap",      m_pHeightmap, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_HeightmapPS",    m_pHeightmap, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_PatchInstances", m_pCulledPatches); // GPU-written by cull CS, read here

		context.DrawMeshTasksIndirectCount(
			m_pIndirectCommands,
			offsetof(IndirectCommandData, groupCountX),
			m_pDrawCountBuffer,
			MAX_CULLED_PATCHES,
			sizeof(IndirectCommandData));
	}
	context.EndRenderPass();

	rt->InvalidateImageLayout();
}

void TerrainNode::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(width);
	UNUSED(height);
	UNUSED(depth);
}


} // namespace baamboo
