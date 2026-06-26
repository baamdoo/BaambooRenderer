#include "BaambooPch.h"
#include "VoxelChunkRenderNode.h"

#include "ShaderTypes.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"

#include "BaambooScene/Scene.h"
#include "BaambooScene/VoxelTerrain/MarchingCubes.h"

namespace baamboo
{


VoxelChunkRenderNode::VoxelChunkRenderNode(render::RenderDevice& rd)
	: Super(rd, "VoxelChunkPass")
{
	using namespace render;

	// Persistent geometry slabs (no index buffer; the mesh-shader path consumes meshlets).
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
			              .SetCullMode(eCullMode::None)                            // closed iso-surface
			              .SetDepthWriteEnable(true, eCompareOp::Greater).Build(); // reversed-Z
	}

	// Density volume + the linear copy the MC extract actually samples (texture is write-only for now).
	const u32 kDensityVoxelCount = kDensityVolumeDim * kDensityVolumeDim * kDensityVolumeDim;
	m_pDensityVolume = Texture::Create(rd, "VoxelChunkPass::DensityVolume",
		{
			.imageType  = eImageType::Texture3D,
			.resolution = uint3(kDensityVolumeDim, kDensityVolumeDim, kDensityVolumeDim),
			.format     = eFormat::R32_FLOAT,
			.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample,
		});
	m_pDensityField = Buffer::Create(rd, "VoxelChunkPass::DensityField",
		{
			.count              = kDensityVoxelCount,
			.elementSizeInBytes = sizeof(float),
			.bufferUsage        = eBufferUsage_Storage,
		});

	auto pDensityCS = Shader::Create(rd, "VoxelDensityCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelDensityCS" });
	m_pDensityPSO = ComputePipeline::Create(rd, "VoxelDensityPSO");
	m_pDensityPSO->SetComputeShader(pDensityCS).Build();

	m_pMCTriTable = Buffer::Create(rd, "VoxelChunkPass::MCTriTable",
		{
			.count              = MarchingCubes::kFlatTriangleTableSize,
			.elementSizeInBytes = sizeof(i32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	m_pMCCounter = Buffer::Create(rd, "VoxelChunkPass::MCCounter",
		{
			.count              = 2, // [triangleCount, activeCellCount]
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest, // TransferDest: ClearBuffer uses vkCmdFillBuffer
		});

	auto pMCExtractCS = Shader::Create(rd, "VoxelMarchingCubesCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelMarchingCubesCS" });
	m_pMCExtractPSO = ComputePipeline::Create(rd, "VoxelMarchingCubesPSO");
	m_pMCExtractPSO->SetComputeShader(pMCExtractCS).Build();

	auto pMeshletBuildCS = Shader::Create(rd, "VoxelMeshletBuildCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelMeshletBuildCS" });
	m_pMeshletBuildPSO = ComputePipeline::Create(rd, "VoxelMeshletBuildPSO");
	m_pMeshletBuildPSO->SetComputeShader(pMeshletBuildCS).Build();

	PublishPools();
}

void VoxelChunkRenderNode::CapturePending(const VoxelTerrainRenderView& vt)
{
	m_PendingOriginWS           = vt.originWorld;
	m_PendingVoxelSize          = vt.voxelSizeMeter;
	m_PendingChunkWorldSz       = vt.chunkWorldSizeMeter;
	m_PendingTerrainInstance    = vt.terrainInstance;
	m_PendingChunkCoord         = vt.chunkCoord;
	m_PendingLOD                = vt.lod;
	m_PendingFieldRevision      = vt.fieldRevision;
	m_PendingExtractionRevision = vt.extractionRevision;
	m_PendingRevision           = vt.revision;
	m_bHasPending               = true;
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

	return kInvalidIndex; // pool exhausted (multi-chunk streaming will grow x2)
}

bool VoxelChunkRenderNode::EnsureChunkResident(render::CommandContext& context)
{
	if (!m_bHasPending)
		return true;

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

	if (!m_bTriTableUploaded)
	{
		std::vector< i32 > triTable(MarchingCubes::kFlatTriangleTableSize);
		MarchingCubes::FillFlatTriangleTable(triTable.data());
		context.UploadData(m_pMCTriTable, triTable.data(), MarchingCubes::kFlatTriangleTableSize, sizeof(i32), 0);
		m_bTriTableUploaded = true;
	}

	// Per-corner meshlets are field-independent: vertices = identity, triangles = repeating {3t,3t+1,3t+2}. Upload once per slab.
	if (!m_bSlabStaticUploaded)
	{
		std::vector< u32 > identityMV(kMeshletVertexSlabCap);
		for (u32 i = 0u; i < kMeshletVertexSlabCap; ++i)
			identityMV[i] = i;

		std::vector< u32 > patternMT(kMeshletTriSlabCap);
		for (u32 p = 0u; p < kMeshletTriSlabCap; ++p)
		{
			const u32 t = p % kTrianglesPerMeshlet;
			patternMT[p] = ((3u * t + 2u) << 16) | ((3u * t + 1u) << 8) | (3u * t);
		}

		context.UploadData(m_pMeshletVertexPool,   identityMV.data(), kMeshletVertexSlabCap, sizeof(u32), (u64)mvBase * sizeof(u32));
		context.UploadData(m_pMeshletTrianglePool, patternMT.data(),  kMeshletTriSlabCap,    sizeof(u32), (u64)mtBase * sizeof(u32));
		m_bSlabStaticUploaded = true;
	}

	// vertexCount/meshletCount stay 0; VoxelMeshletBuildCS patches them after extraction.
	VoxelChunk row = {};
	row.originWS              = m_PendingOriginWS;
	row.voxelSizeMeter        = m_PendingVoxelSize;
	row.aabbMin               = m_PendingOriginWS;
	row.aabbMax               = m_PendingOriginWS + float3(m_PendingChunkWorldSz);
	row.lodDepth              = m_PendingLOD;
	row.transitionMask        = 0u;
	row.vertexOffset          = vBase;
	row.vertexCount           = 0u;
	row.meshletOffset         = mlBase;
	row.meshletCount          = 0u;
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

	m_NumChunks        = 1u;
	m_BuiltRevision    = m_PendingRevision;
	m_RejectedRevision = kInvalidIndex;
	m_bHasPending      = false;
	return true;
}

void VoxelChunkRenderNode::DiscardPending(u32 rejectedRevision)
{
	m_bHasPending      = false;
	m_PendingRevision  = kInvalidIndex;
	m_RejectedRevision = rejectedRevision;
}

void VoxelChunkRenderNode::DispatchDensity(render::CommandContext& context, const VoxelTerrainRenderView& vt)
{
	using namespace render;
	if (!m_pDensityPSO || !m_pDensityVolume)
		return;

	const u32 dim = vt.genParams.samplesPerAxis + 2u * vt.genParams.apron; // C+1+2A
	if (dim == 0u || dim > kDensityVolumeDim)
	{
		fprintf(stderr, "[VoxelDensity] dim %u exceeds volume %u -- density skipped.\n", dim, kDensityVolumeDim);
		return;
	}

	context.SetRenderPipeline(m_pDensityPSO.get());

	context.TransitionBarrier(m_pDensityVolume, eTextureLayout::General);
	context.TransitionBufferToWrite(m_pDensityField, ePipelineStage::ComputeShader);

	context.SetComputeDynamicUniformBuffer("g_VoxelGenParams", vt.genParams);
	context.StageDescriptor("g_OutDensityTex",   m_pDensityVolume);
	context.StageDescriptor("g_OutDensityDebug", m_pDensityField);

	context.Dispatch3D< 4, 4, 4 >(dim, dim, dim);

	context.TransitionBarrier(m_pDensityVolume, eTextureLayout::ShaderReadOnly);
}

void VoxelChunkRenderNode::DispatchExtraction(render::CommandContext& context, const VoxelTerrainRenderView& vt)
{
	using namespace render;
	if (!m_pMCExtractPSO || !m_pMeshletBuildPSO || m_ChunkSlabId == kInvalidIndex)
		return;

	const u32 C      = vt.genParams.cellsPerAxis;
	const u32 apron  = vt.genParams.apron;
	const u32 vBase  = m_ChunkSlabId * kVertexSlabCapacity;
	const u32 mlBase = m_ChunkSlabId * kMeshletSlabCapacity;
	if (C == 0u)
		return;

	// Pass A: marching cubes -- each active cell atomic-appends its per-corner triangle vertices to the slab.
	context.ClearBuffer(m_pMCCounter, 0u); // [triangleCount, activeCellCount]

	context.TransitionBufferToRead(m_pMCTriTable, ePipelineStage::ComputeShader);
	context.TransitionBufferToRead(m_pDensityField, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pMCCounter, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pVertexPool, ePipelineStage::ComputeShader);

	context.SetRenderPipeline(m_pMCExtractPSO.get());
	struct
	{
		u32   cellsPerAxis;
		u32   apron;
		float voxelSizeMeter;
		u32   vertexSlabBase;
		u32   maxTriangles;
	} mc = { C, apron, vt.genParams.voxelSizeMeter, vBase, kMaxTrianglesPerChunk };
	context.SetComputeConstants(sizeof(mc), &mc);
	context.StageDescriptor("g_TriTable",     m_pMCTriTable);
	context.StageDescriptor("g_DensityField", m_pDensityField);
	context.StageDescriptor("g_MCCounter",    m_pMCCounter);
	context.StageDescriptor("g_OutVertices",  m_pVertexPool);
	context.Dispatch3D< 4, 4, 4 >(C, C, C);

	context.UAVBarrier(m_pMCCounter, true); // extract -> meshlet build
	context.UAVBarrier(m_pVertexPool);

	// Pass B: pack sequential meshlets and patch vertexCount/meshletCount into the chunk row.
	context.TransitionBufferToWrite(m_pMeshletPool, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pChunkTableBuffer, ePipelineStage::ComputeShader);

	context.SetRenderPipeline(m_pMeshletBuildPSO.get());
	struct
	{
		u32 chunkID;
		u32 meshletSlabBase;
		u32 trianglesPerMeshlet;
		u32 maxMeshlets;
		u32 maxTriangles;
	} mb = { 0u, mlBase, kTrianglesPerMeshlet, kMeshletSlabCapacity, kMaxTrianglesPerChunk };
	context.SetComputeConstants(sizeof(mb), &mb);
	context.StageDescriptor("g_MCCounter",   m_pMCCounter);
	context.StageDescriptor("g_OutMeshlets", m_pMeshletPool);
	context.StageDescriptor("g_OutChunks",   m_pChunkTableBuffer);
	context.Dispatch1D< 64 >(kMeshletSlabCapacity);

	context.UAVBarrier(m_pMeshletPool);
	context.UAVBarrier(m_pChunkTableBuffer, true);
}

void VoxelChunkRenderNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(context);
	UNUSED(renderView);
}

void VoxelChunkRenderNode::DispatchChunkCull(render::CommandContext& context, u32 phase, const Arc< render::Texture >& pHiZTexture, const SceneRenderView& renderView)
{
	UNUSED(pHiZTexture); // HiZ occlusion deferred to a later (multi-chunk) phase
	using namespace render;

	const VoxelTerrainRenderView& vt = renderView.voxelTerrain;
	if (vt.bValid &&
		vt.revision != m_BuiltRevision &&
		vt.revision != m_PendingRevision &&
		vt.revision != m_RejectedRevision)
	{
		// GPU build: density volume -> marching cubes -> vertex/meshlet pools + row count patch.
		CapturePending(vt);
		if (EnsureChunkResident(context))
		{
			DispatchDensity(context, vt);
			DispatchExtraction(context, vt);
		}
	}

	if (phase != 0u /* CullingNode::kPhase1Cull */)
		return;

	context.ClearBuffer(m_pDrawCountBuffer, 0);

	if (m_NumChunks == 0u) // no resident chunk -> the cull CS would emit no draws; skip it
	{
		context.TransitionBufferToRead(m_pDrawCountBuffer, ePipelineStage::DrawIndirect, 0, true);
		return;
	}

	context.SetRenderPipeline(m_pChunkCullingPSO.get());

	context.TransitionBufferToWrite(m_pIndirectCommands, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pDrawCountBuffer, ePipelineStage::ComputeShader);
	context.TransitionBufferToRead(m_pChunkTableBuffer, ePipelineStage::ComputeShader);

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
