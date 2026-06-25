#include "BaambooPch.h"
#include "VoxelChunkRenderNode.h"

#include "ShaderTypes.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"

#include "BaambooScene/Scene.h"
#include "BaambooScene/VoxelTerrain/SDFChunk.h"

namespace baamboo
{


VoxelChunkRenderNode::VoxelChunkRenderNode(render::RenderDevice& rd)
	: Super(rd, "VoxelChunkPass")
{
	using namespace render;

	m_pVertexPool = Buffer::Create(rd, "VoxelChunkPass::VertexPool",
		{
			.count              = kMaxResidentSlabs * kVertexSlabCapacity,
			.elementSizeInBytes = sizeof(::Vertex),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	m_pMeshletPool = Buffer::Create(rd, "VoxelChunkPass::MeshletPool",
		{
			.count              = kMaxResidentSlabs * kMeshletSlabCapacity,
			.elementSizeInBytes = sizeof(Meshlet),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	m_pMeshletVertexPool = Buffer::Create(rd, "VoxelChunkPass::MeshletVertexPool",
		{
			.count              = kMaxResidentSlabs * kMeshletVertexSlabCap,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	m_pMeshletTrianglePool = Buffer::Create(rd, "VoxelChunkPass::MeshletTrianglePool",
		{
			.count              = kMaxResidentSlabs * kMeshletTriSlabCap,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	m_pChunkTableBuffer = Buffer::Create(rd, "VoxelChunkPass::ChunkTable",
		{
			.count              = kMaxChunks,
			.elementSizeInBytes = sizeof(VoxelChunk),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	m_pIndirectCommands = Buffer::Create(rd, "VoxelChunkPass::IndirectCommands",
		{
			.count              = kMaxChunks,
			.elementSizeInBytes = sizeof(IndirectCommandData),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_Indirect,
		});
	m_pDrawCountBuffer = Buffer::Create(rd, "VoxelChunkPass::DrawCount",
		{
			.count              = 1,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_Indirect
			                    | eBufferUsage_TransferSource | eBufferUsage_TransferDest
			                    | eBufferUsage_ShaderDeviceAddress,
		});

	m_FreeSlabs.reserve(kMaxResidentSlabs);

	// --- Pipelines ---
	auto pCullingCS = Shader::Create(rd, "VoxelChunkCullingCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelChunkCullingCS" });
	m_pChunkCullingPSO = ComputePipeline::Create(rd, "VoxelChunkCullingPSO");
	m_pChunkCullingPSO->SetComputeShader(pCullingCS).Build();

	auto pSharedRT = g_FrameData.pPhase2Draw.lock();
	if (pSharedRT)
	{
		auto pMS = Shader::Create(rd, "VoxelGBufferMS", { .stage = eShaderStage::Mesh,     .filename = "VoxelGBufferMS" });
		auto pPS = Shader::Create(rd, "GBufferPS",      { .stage = eShaderStage::Fragment, .filename = "GBufferPS" });

		m_pVoxelGBufferPSO = GraphicsPipeline::Create(rd, "VoxelGBufferPSO");
		m_pVoxelGBufferPSO->SetMeshShaders(pMS, pPS)
			              .SetRenderTarget(pSharedRT)
			              .SetCullMode(eCullMode::None) // closed iso-surface; winding handled by reversed-Z depth
			              .SetDepthWriteEnable(true, eCompareOp::Greater).Build(); // reversed-Z
	}

	PublishPools();
}

bool VoxelChunkRenderNode::SetOracleChunk(const VoxelTerrainChunkDesc& desc, const VoxelTerrainRenderView& terrainView)
{
	SDFChunk chunk(desc);
	if (!chunk.BuildSamples() || !chunk.BuildMesh())
	{
		fprintf(stderr, "[VoxelChunkRenderNode] oracle chunk generation failed.\n");
		return false;
	}

	m_PendingMesh               = chunk.MeshData(); // copy (vertices/meshlets/...)
	m_PendingOriginWS           = desc.originWorld;
	m_PendingVoxelSize          = desc.settings.voxelSizeMeter;
	m_PendingChunkWorldSz       = desc.settings.chunkWorldSizeMeter;
	m_PendingTerrainInstance    = terrainView.terrainInstance;
	m_PendingChunkCoord         = terrainView.chunkCoord;
	m_PendingLOD                = terrainView.lod;
	m_PendingFieldRevision      = terrainView.fieldRevision;
	m_PendingExtractionRevision = terrainView.extractionRevision;
	m_PendingRevision           = terrainView.revision;
	m_bHasPending               = true;
	return true;
}

u32 VoxelChunkRenderNode::AllocateSlab()
{
	if (!m_FreeSlabs.empty())
	{
		const u32 slab = m_FreeSlabs.back();
		m_FreeSlabs.pop_back();
		return slab;
	}
	if (m_SlabHighWater < kMaxResidentSlabs)
		return m_SlabHighWater++;

	return kInvalidIndex; // pool exhausted (Phase 5 streaming will grow x2)
}

bool VoxelChunkRenderNode::EnsureUpload(render::CommandContext& context)
{
	if (!m_bHasPending)
		return true;

	const u32 numV  = m_PendingMesh.NumVertices();
	const u32 numML = m_PendingMesh.NumMeshlets();
	const u32 numMV = static_cast< u32 >(m_PendingMesh.meshletVertices.size());
	const u32 numMT = static_cast< u32 >(m_PendingMesh.meshletTriangles.size());

	if (numV == 0u || numML == 0u)
	{
		m_NumChunks         = 0u; // empty chunk: valid payload with no draw work
		m_ChunkMeshletCount = 0u;
		m_BuiltRevision     = m_PendingRevision;
		m_RejectedRevision  = kInvalidIndex;
		m_bHasPending       = false;
		return true;
	}

	if (numV > kVertexSlabCapacity || numMV > kMeshletVertexSlabCap || numMT > kMeshletTriSlabCap  || numML > kMeshletSlabCapacity)
	{
		fprintf(stderr,
			"[VoxelChunkRenderNode] chunk exceeds slab capacity "
			"(V %u/%u, MV %u/%u, MT %u/%u, ML %u/%u) — skipped; bump *SlabCapacity.\n",
			numV, kVertexSlabCapacity, numMV, kMeshletVertexSlabCap,
			numMT, kMeshletTriSlabCap, numML, kMeshletSlabCapacity);
		DiscardPending(m_PendingRevision);
		return false;
	}

	if (m_ChunkSlabId == kInvalidIndex)
		m_ChunkSlabId = AllocateSlab();
	if (m_ChunkSlabId == kInvalidIndex)
	{
		fprintf(stderr, "[VoxelChunkRenderNode] slab pool exhausted.\n");
		DiscardPending(m_PendingRevision);
		return false;
	}

	const u32 vBase  = m_ChunkSlabId * kVertexSlabCapacity;
	const u32 mvBase = m_ChunkSlabId * kMeshletVertexSlabCap;
	const u32 mtBase = m_ChunkSlabId * kMeshletTriSlabCap;
	const u32 mlBase = m_ChunkSlabId * kMeshletSlabCapacity;

	context.UploadData(m_pVertexPool,          m_PendingMesh.vertices.data(),         numV,  sizeof(::Vertex),  (u64)vBase  * sizeof(::Vertex));
	context.UploadData(m_pMeshletVertexPool,   m_PendingMesh.meshletVertices.data(),  numMV, sizeof(u32),     (u64)mvBase * sizeof(u32));
	context.UploadData(m_pMeshletTrianglePool, m_PendingMesh.meshletTriangles.data(), numMT, sizeof(u32),     (u64)mtBase * sizeof(u32));
	context.UploadData(m_pMeshletPool,         m_PendingMesh.meshlets.data(),         numML, sizeof(Meshlet), (u64)mlBase * sizeof(Meshlet));

	VoxelChunk row = {};
	row.originWS              = m_PendingOriginWS;
	row.voxelSizeMeter        = m_PendingVoxelSize;
	row.aabbMin               = m_PendingOriginWS;
	row.aabbMax               = m_PendingOriginWS + float3(m_PendingChunkWorldSz);
	row.lodDepth              = m_PendingLOD;
	row.transitionMask        = 0u;
	row.vertexOffset          = vBase;
	row.vertexCount           = numV;
	row.meshletOffset         = mlBase;
	row.meshletCount          = numML;
	row.meshletVertexOffset   = mvBase;
	row.meshletTriangleOffset = mtBase;
	row.slabId                = m_ChunkSlabId;
	row.padding0              = 0u;
	row.terrainInstanceLo    = static_cast< u32 >(m_PendingTerrainInstance & 0xFFFFFFFFull);
	row.terrainInstanceHi    = static_cast< u32 >((m_PendingTerrainInstance >> 32) & 0xFFFFFFFFull);
	row.chunkCoordX          = m_PendingChunkCoord.x;
	row.chunkCoordY          = m_PendingChunkCoord.y;
	row.chunkCoordZ          = m_PendingChunkCoord.z;
	row.lod                  = m_PendingLOD;
	row.fieldRevision        = m_PendingFieldRevision;
	row.extractionRevision   = m_PendingExtractionRevision;
	context.UploadData(m_pChunkTableBuffer, &row, 1, sizeof(VoxelChunk), 0);

	m_NumChunks         = 1u;
	m_ChunkMeshletCount = numML;
	m_BuiltRevision     = m_PendingRevision;
	m_RejectedRevision  = kInvalidIndex;
	m_bHasPending       = false;
	return true;
}

void VoxelChunkRenderNode::DiscardPending(u32 rejectedRevision)
{
	m_bHasPending      = false;
	m_PendingRevision  = kInvalidIndex;
	m_RejectedRevision = rejectedRevision;
}

void VoxelChunkRenderNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(context);
	UNUSED(renderView);
}

void VoxelChunkRenderNode::DispatchChunkCull(render::CommandContext& context, u32 phase, const Arc< render::Texture >& pHiZTexture, const SceneRenderView& renderView)
{
	UNUSED(pHiZTexture); // HiZ occlusion deferred to Phase 5 (multi-chunk).
	using namespace render;

	const VoxelTerrainRenderView& vt = renderView.voxelTerrain;
	if (vt.bValid &&
		vt.revision != m_BuiltRevision &&
		vt.revision != m_PendingRevision &&
		vt.revision != m_RejectedRevision)
	{
		VoxelTerrainChunkDesc desc;
		desc.originWorld                      = vt.originWorld;
		desc.settings.chunkWorldSizeMeter     = vt.chunkWorldSizeMeter;
		desc.settings.voxelSizeMeter          = vt.voxelSizeMeter;
		desc.settings.cellsPerAxis            = vt.cellsPerAxis;
		desc.settings.samplesPerAxis          = vt.samplesPerAxis;
		desc.settings.normalEpsilonMultiplier = vt.normalEpsilonMultiplier;
		desc.SDF                              = vt.SDF;
		if (!SetOracleChunk(desc, vt))
			DiscardPending(vt.revision);
	}

	EnsureUpload(context);

	if (phase != 0u /* CullingNode::kPhase1Cull */)
		return;

	context.ClearBuffer(m_pDrawCountBuffer, 0);

	if (m_NumChunks == 0u || m_ChunkMeshletCount == 0u)
	{
		context.TransitionBufferToRead(m_pDrawCountBuffer, ePipelineStage::DrawIndirect, 0, true);
		return;
	}

	context.SetRenderPipeline(m_pChunkCullingPSO.get());

	context.TransitionBufferToWrite(m_pIndirectCommands, ePipelineStage::ComputeShader); // (c) producer side
	context.TransitionBufferToWrite(m_pDrawCountBuffer, ePipelineStage::ComputeShader); // (b) clear -> cull write
	context.TransitionBufferToRead(m_pChunkTableBuffer, ePipelineStage::ComputeShader); // (a) upload -> cull read

	struct
	{
		u32 numChunks;
		u32 cullingPhase;
	} constant = {
		.numChunks    = m_NumChunks,
		.cullingPhase = phase,
	};
	context.SetComputeConstants(sizeof(constant), &constant);

	context.StageDescriptor("g_VoxelChunks",      m_pChunkTableBuffer);
	context.StageDescriptor("g_IndirectCommands", m_pIndirectCommands);
	context.StageDescriptor("g_DrawCount",        m_pDrawCountBuffer);

	context.Dispatch1D< 64 >(m_NumChunks);

	context.TransitionBufferToRead(m_pIndirectCommands, ePipelineStage::DrawIndirect);
	context.TransitionBufferToIndirectArgs(m_pDrawCountBuffer, 0, true);
}

void VoxelChunkRenderNode::DrawChunksPhase1(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView)
{
	DrawChunksImpl(context, rt, renderView);
}

void VoxelChunkRenderNode::DrawChunksPhase2(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView)
{
	UNUSED(context);
	UNUSED(rt);
	UNUSED(renderView);
}

void VoxelChunkRenderNode::DrawChunksImpl(render::CommandContext& context, Arc< render::RenderTarget > rt, const SceneRenderView& renderView)
{
	using namespace render;
	UNUSED(renderView);

	if (!m_pVoxelGBufferPSO || !rt || m_NumChunks == 0u)
		return;

	context.TransitionBufferToRead(m_pChunkTableBuffer,      ePipelineStage::MeshShader);
	context.TransitionBufferToRead(m_pVertexPool,            ePipelineStage::MeshShader);
	context.TransitionBufferToRead(m_pMeshletPool,           ePipelineStage::MeshShader);
	context.TransitionBufferToRead(m_pMeshletVertexPool,     ePipelineStage::MeshShader);
	context.TransitionBufferToRead(m_pMeshletTrianglePool,   ePipelineStage::MeshShader);

	context.BeginRenderPass(rt);
	{
		context.SetRenderPipeline(m_pVoxelGBufferPSO.get());

		context.StageDescriptor("g_VoxelChunks",           m_pChunkTableBuffer);
		context.StageDescriptor("g_VoxelVertices",         m_pVertexPool);
		context.StageDescriptor("g_VoxelMeshlets",         m_pMeshletPool);
		context.StageDescriptor("g_VoxelMeshletVertices",  m_pMeshletVertexPool);
		context.StageDescriptor("g_VoxelMeshletTriangles", m_pMeshletTrianglePool);

		context.DrawMeshTasksIndirectCount(
			m_pIndirectCommands,
			offsetof(IndirectCommandData, groupCountX),
			m_pDrawCountBuffer,
			kMaxChunks,
			sizeof(IndirectCommandData));
	}
	context.EndRenderPass();

	rt->InvalidateImageLayout();

	PublishPools();
}

void VoxelChunkRenderNode::PublishPools()
{
	g_FrameData.pVoxelChunks           = m_pChunkTableBuffer;
	g_FrameData.pVoxelVertices         = m_pVertexPool;
	g_FrameData.pVoxelMeshlets         = m_pMeshletPool;
	g_FrameData.pVoxelMeshletVertices  = m_pMeshletVertexPool;
	g_FrameData.pVoxelMeshletTriangles = m_pMeshletTrianglePool;
}

void VoxelChunkRenderNode::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(width);
	UNUSED(height);
	UNUSED(depth);
}


} // namespace baamboo
