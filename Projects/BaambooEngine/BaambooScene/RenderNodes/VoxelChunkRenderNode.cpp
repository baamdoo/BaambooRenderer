#include "BaambooPch.h"
#include "VoxelChunkRenderNode.h"

#include "ShaderTypes.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"

#include "BaambooScene/Scene.h"
#include "BaambooScene/VoxelTerrain/MarchingCubes.h"

namespace baamboo
{


// ---- Construction ---------------------------------------------------------

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

	// Density volume + the linear copy the MC extract samples.
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

	// Triangle spatial sort (count -> scan -> scatter), between MC extract and meshlet build.
	m_pTriSortBins = Buffer::Create(rd, "VoxelChunkPass::TriSortBins",
		{
			.count              = kTriSortBins,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	auto pTriSortCountCS = Shader::Create(rd, "VoxelTriSortCountCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelTriSortCountCS" });
	m_pTriSortCountPSO = ComputePipeline::Create(rd, "VoxelTriSortCountPSO");
	m_pTriSortCountPSO->SetComputeShader(pTriSortCountCS).Build();

	auto pTriSortScanCS = Shader::Create(rd, "VoxelTriSortScanCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelTriSortScanCS" });
	m_pTriSortScanPSO = ComputePipeline::Create(rd, "VoxelTriSortScanPSO");
	m_pTriSortScanPSO->SetComputeShader(pTriSortScanCS).Build();

	auto pTriSortScatterCS = Shader::Create(rd, "VoxelTriSortScatterCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelTriSortScatterCS" });
	m_pTriSortScatterPSO = ComputePipeline::Create(rd, "VoxelTriSortScatterPSO");
	m_pTriSortScatterPSO->SetComputeShader(pTriSortScatterCS).Build();

	m_pErosionDetailMap = Texture::Create(rd, "VoxelChunkPass::ErosionDetailMap",
		{
			.resolution = uint3(kErosionMapDim, kErosionMapDim, 1),
			.format     = eFormat::RGBA16_FLOAT,
			.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample,
		});
	auto pErosionBakeCS = Shader::Create(rd, "VoxelErosionBakeCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelErosionBakeCS" });
	m_pErosionBakePSO = ComputePipeline::Create(rd, "VoxelErosionBakePSO");
	m_pErosionBakePSO->SetComputeShader(pErosionBakeCS).Build();

	g_FrameData.pVoxelChunkCounts      = m_pChunkCountsBuffer;
	g_FrameData.pVoxelVertices         = m_pVertexPool;
	g_FrameData.pVoxelMeshlets         = m_pMeshletPool;
	g_FrameData.pVoxelMeshletVertices  = m_pMeshletVertexPool;
	g_FrameData.pVoxelMeshletTriangles = m_pMeshletTrianglePool;
	g_FrameData.pVoxelErosionDetail    = m_pErosionDetailMap;
}

// ---- Chunk residency ------------------------------------------------------

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

	return kInvalidIndex; // pool exhausted
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
	desc.chunkSizeMeter        = float(vt.genParams.cellsPerAxis) * vt.genParams.voxelSizeMeter;
	desc.voxelSizeMeter        = vt.genParams.voxelSizeMeter;
	g_FrameData.voxelChunkDesc = desc; // live dice/micro params refresh every frame

	m_BuiltRevision = vt.revision;
	return true;
}

// ---- GPU dispatch passes ----------------------------------------------------

// This pass only draws the height field on XZ plane so it currently wastes y-samples.
// TODO: [2-pass] 1) compute height field on XZ plane, 2) compute density by sampling the height field
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

	context.UAVBarrier(m_pMCCounter, true); // extract -> sort
	context.UAVBarrier(m_pVertexPool);

	// Pass A2: triangle spatial sort into Morton blocks, baked into the meshlet-vertex indirection (vertices stay in place).
	const u32 mvBase = m_ChunkSlabId * kMeshletVertexSlabCap;

	context.ClearBuffer(m_pTriSortBins, 0u);

	context.TransitionBufferToRead(m_pVertexPool, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pTriSortBins, ePipelineStage::ComputeShader);

	struct
	{
		u32   vertexSlabBase;
		u32   meshletVertexSlabBase;
		u32   maxTriangles;
		float chunkSizeMeter;
	} ts = { vBase, mvBase, kMaxTrianglesPerChunk, float(C) * vt.genParams.voxelSizeMeter };

	// A2.1: histogram
	context.SetRenderPipeline(m_pTriSortCountPSO.get());
	context.SetComputeConstants(sizeof(ts), &ts);
	context.StageDescriptor("g_MCCounter", m_pMCCounter);
	context.StageDescriptor("g_Vertices",  m_pVertexPool);
	context.StageDescriptor("g_SortBins",  m_pTriSortBins);
	context.Dispatch1D< 256 >(kMaxTrianglesPerChunk);

	context.UAVBarrier(m_pTriSortBins, true);

	// A2.2: exclusive scan (single group)
	static_assert(kTriSortBins % 1024u == 0u, "scan CS strips assume bins % threads == 0");
	context.SetRenderPipeline(m_pTriSortScanPSO.get());
	struct { u32 numBins; } sc = { kTriSortBins };
	context.SetComputeConstants(sizeof(sc), &sc);
	context.StageDescriptor("g_SortBins", m_pTriSortBins);
	context.Dispatch1D< 1024 >(1024u);

	context.UAVBarrier(m_pTriSortBins, true);

	// A2.3: scatter the permutation into the meshlet-vertex indirection
	context.TransitionBufferToWrite(m_pMeshletVertexPool, ePipelineStage::ComputeShader);

	context.SetRenderPipeline(m_pTriSortScatterPSO.get());
	context.SetComputeConstants(sizeof(ts), &ts);
	context.StageDescriptor("g_MCCounter",       m_pMCCounter);
	context.StageDescriptor("g_Vertices",        m_pVertexPool);
	context.StageDescriptor("g_SortBins",        m_pTriSortBins);
	context.StageDescriptor("g_OutMeshletVerts", m_pMeshletVertexPool);
	context.Dispatch1D< 256 >(kMaxTrianglesPerChunk);

	context.UAVBarrier(m_pMeshletVertexPool, true); // sort -> meshlet build

	// Pass B: pack sequential meshlets (+ bounds through the sorted indirection) and patch counts.
	context.TransitionBufferToWrite(m_pMeshletPool, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pChunkCountsBuffer, ePipelineStage::ComputeShader);
	context.TransitionBufferToRead(m_pMeshletVertexPool, ePipelineStage::ComputeShader);

	context.SetRenderPipeline(m_pMeshletBuildPSO.get());
	struct
	{
		u32 chunkID;
		u32 meshletSlabBase;
		u32 trianglesPerMeshlet;
		u32 maxMeshlets;
		u32 maxTriangles;
		u32 vertexSlabBase;
		u32 meshletVertexSlabBase;
	} mb = { 0u, mlBase, kTrianglesPerMeshlet, kMeshletSlabCapacity, kMaxTrianglesPerChunk, vBase, mvBase };
	context.SetComputeConstants(sizeof(mb), &mb);
	context.StageDescriptor("g_MCCounter", m_pMCCounter);
	context.StageDescriptor("g_OutMeshlets", m_pMeshletPool);
	context.StageDescriptor("g_OutCounts", m_pChunkCountsBuffer);
	context.StageDescriptor("g_Vertices", m_pVertexPool);
	context.StageDescriptor("g_MeshletVerts", m_pMeshletVertexPool);
	context.Dispatch1D< 64 >(kMeshletSlabCapacity);

	context.UAVBarrier(m_pMeshletPool);
	context.UAVBarrier(m_pChunkCountsBuffer, true);
}

void VoxelChunkRenderNode::DispatchErosionBake(render::CommandContext& context, const VoxelTerrainRenderView& vt)
{
	using namespace render;
	if (!m_pErosionBakePSO || !m_pErosionDetailMap)
		return;

	context.SetRenderPipeline(m_pErosionBakePSO.get());

	context.TransitionBarrier(m_pErosionDetailMap, eTextureLayout::General);

	context.SetComputeDynamicUniformBuffer("g_VoxelGenParams", vt.genParams);
	context.StageDescriptor("g_OutErosionMap", m_pErosionDetailMap);

	context.Dispatch2D< 8, 8 >(kErosionMapDim, kErosionMapDim);

	context.TransitionBarrier(m_pErosionDetailMap, eTextureLayout::ShaderReadOnly);
}

// ---- Frame entry points -----------------------------------------------------

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
			DispatchErosionBake(context, vt); // shading-band erosion detail (lighting consumes)
		}
	}

	if (vt.bValid)
	{
		VoxelChunkDesc& desc = g_FrameData.voxelChunkDesc;
		desc.diceMaxLevel          = vt.dice.maxLevel;
		desc.diceTargetPx          = vt.dice.targetPx;
		desc.diceRadiusMeter       = vt.dice.radiusM;
		desc.diceFadeWidthMeter    = vt.dice.fadeWidthMeter;
		desc.diceDisplacementScale = vt.dice.displacementScale;
		desc.debugFlags            = vt.dice.debugFlags;

		const CameraRenderView& cam = renderView.bFrozen ? renderView.frozenCamera : renderView.camera;
		const float2 viewport = renderView.bFrozen ? renderView.frozenViewport : renderView.viewport;
		desc.diceKScale = 0.5f * viewport.y * std::abs(cam.mProj[1][1]);

		desc.microAmplitudeMeter      = vt.dice.microAmplitudeMeter;
		desc.microBaseWaveLengthMeter = vt.dice.microBaseWaveLengthMeter;
		desc.microLacunarity          = vt.dice.microLacunarity;
		desc.microGain                = vt.dice.microGain;
		desc.microCreaseBoost         = vt.dice.microCreaseBoost;
		desc.microSharpness           = vt.dice.microSharpness;
		desc.microOctaves             = vt.dice.microOctaves;
	}
}

void VoxelChunkRenderNode::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(width);
	UNUSED(height);
	UNUSED(depth);
}


} // namespace baamboo
