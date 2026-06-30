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

	m_pChunkCountsBuffer = Buffer::Create(rd, "VoxelChunkPass::ChunkCounts",
		{
			.count              = kMaxChunks,
			.elementSizeInBytes = sizeof(VoxelChunkCounts),
			.bufferUsage        = eBufferUsage_Storage,
		});
	m_FreeSlabs.reserve(kMaxResidentSlabs);

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
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	auto pMCExtractCS = Shader::Create(rd, "VoxelMarchingCubesCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelMarchingCubesCS" });
	m_pMCExtractPSO = ComputePipeline::Create(rd, "VoxelMarchingCubesPSO");
	m_pMCExtractPSO->SetComputeShader(pMCExtractCS).Build();

	auto pMeshletBuildCS = Shader::Create(rd, "VoxelMeshletBuildCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelMeshletBuildCS" });
	m_pMeshletBuildPSO = ComputePipeline::Create(rd, "VoxelMeshletBuildPSO");
	m_pMeshletBuildPSO->SetComputeShader(pMeshletBuildCS).Build();

	g_FrameData.pVoxelChunkCounts      = m_pChunkCountsBuffer;
	g_FrameData.pVoxelVertices         = m_pVertexPool;
	g_FrameData.pVoxelMeshlets         = m_pMeshletPool;
	g_FrameData.pVoxelMeshletVertices  = m_pMeshletVertexPool;
	g_FrameData.pVoxelMeshletTriangles = m_pMeshletTrianglePool;
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

bool VoxelChunkRenderNode::EnsureChunkResident(render::CommandContext& context, const VoxelTerrainRenderView& vt)
{
	if (m_ChunkSlabId == kInvalidIndex)
		m_ChunkSlabId = AllocateSlab();
	if (m_ChunkSlabId == kInvalidIndex)
	{
		fprintf(stderr, "[VoxelChunkRenderNode] slab pool exhausted.\n");
		return false;
	}

	const u32 vBase  = m_ChunkSlabId * kVertexSlabCapacity;
	const u32 mvBase = m_ChunkSlabId * kMeshletVertexSlabCap;
	const u32 mtBase = m_ChunkSlabId * kMeshletTriSlabCap;

	if (!m_bTriTableUploaded)
	{
		std::vector< i32 > triTable(MarchingCubes::kFlatTriangleTableSize);
		MarchingCubes::FillFlatTriangleTable(triTable.data());
		context.UploadData(m_pMCTriTable, triTable.data(), MarchingCubes::kFlatTriangleTableSize, sizeof(i32), 0);
		m_bTriTableUploaded = true;
	}

	// Per-corner meshlets are field-independent: vertices = identity, triangles = repeating {3t,3t+1,3t+2}. Upload once per slab.
	if (!m_SlabStaticUploaded[m_ChunkSlabId])
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
		m_SlabStaticUploaded[m_ChunkSlabId] = true;
	}

	VoxelChunkDesc desc = {};
	desc.originWS              = vt.originWorld;
	desc.vertexOffset          = vBase;
	desc.meshletVertexOffset   = mvBase;
	desc.meshletTriangleOffset = mtBase;
	g_FrameData.voxelChunkDesc = desc;

	m_BuiltRevision = vt.revision;
	return true;
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
	context.TransitionBufferToWrite(m_pChunkCountsBuffer, ePipelineStage::ComputeShader);

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
	context.StageDescriptor("g_MCCounter", m_pMCCounter);
	context.StageDescriptor("g_OutMeshlets", m_pMeshletPool);
	context.StageDescriptor("g_OutCounts", m_pChunkCountsBuffer);
	context.Dispatch1D< 64 >(kMeshletSlabCapacity);

	context.UAVBarrier(m_pMeshletPool);
	context.UAVBarrier(m_pChunkCountsBuffer, true);
}

void VoxelChunkRenderNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(context);
	UNUSED(renderView);
}

void VoxelChunkRenderNode::BuildChunkGeometryIfNeeded(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	const VoxelTerrainRenderView& vt = renderView.voxelTerrain;
	if (vt.bValid &&
		vt.revision != m_BuiltRevision)
	{
		// GPU build: density volume -> marching cubes -> vertex/meshlet pools + row count patch.
		if (EnsureChunkResident(context, vt))
		{
			DispatchDensity(context, vt);
			DispatchExtraction(context, vt);
		}
	}
}

void VoxelChunkRenderNode::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(width);
	UNUSED(height);
	UNUSED(depth);
}


} // namespace baamboo
