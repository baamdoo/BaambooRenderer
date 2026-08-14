#include "BaambooPch.h"
#include "VoxelChunkRenderNode.h"

#include "ShaderTypes.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"

#include "BaambooScene/Scene.h"
#include "BaambooScene/VoxelTerrain/MarchingCubes.h"

#include <imgui.h>
#include <fstream>
#include <filesystem>

namespace baamboo
{

namespace
{

bool IsChunksOverlapped(VoxelChunkID chunkA, VoxelChunkID chunkB)
{
	if (chunkA.lod > chunkB.lod)
	{
		const u32 d = chunkA.lod - chunkB.lod;
		return int3(chunkB.coord.x >> d, chunkB.coord.y >> d, chunkB.coord.z >> d) == chunkA.coord;
	}
	else if (chunkA.lod < chunkB.lod)
	{
		const u32 d = chunkB.lod - chunkA.lod;
		return int3(chunkA.coord.x >> d, chunkA.coord.y >> d, chunkA.coord.z >> d) == chunkB.coord;
	}

	return false;
}

bool IsBuilt(const VoxelChunkRenderNode::ChunkSlot& chunkSlot)
{
	using eChunkState = VoxelChunkRenderNode::eChunkState;
	return chunkSlot.state == eChunkState::Resident || chunkSlot.state == eChunkState::Dirty || chunkSlot.state == eChunkState::ResidentEmpty;
}

bool AreChunksFaceAdjacent(VoxelChunkID chunkA, VoxelChunkID chunkB)
{
	int3 finestChunkBeginA = chunkA.coord * (1 << chunkA.lod);
	int3 finestChunkEndA   = (chunkA.coord + 1) * (1 << chunkA.lod);

	int3 finestChunkBeginB = chunkB.coord * (1 << chunkB.lod);
	int3 finestChunkEndB   = (chunkB.coord + 1) * (1 << chunkB.lod);

	// faces share area only if the spans overlap on the two non-touching axes (strict: edge/corner contact excluded)
	const bool bOverlapX = finestChunkBeginA.x < finestChunkEndB.x && finestChunkBeginB.x < finestChunkEndA.x;
	const bool bOverlapY = finestChunkBeginA.y < finestChunkEndB.y && finestChunkBeginB.y < finestChunkEndA.y;
	const bool bOverlapZ = finestChunkBeginA.z < finestChunkEndB.z && finestChunkBeginB.z < finestChunkEndA.z;

	bool bAdjacent = false;
	bAdjacent |= (finestChunkEndA.x == finestChunkBeginB.x || finestChunkBeginA.x == finestChunkEndB.x) && bOverlapY && bOverlapZ; // x
	bAdjacent |= (finestChunkEndA.y == finestChunkBeginB.y || finestChunkBeginA.y == finestChunkEndB.y) && bOverlapX && bOverlapZ; // y
	bAdjacent |= (finestChunkEndA.z == finestChunkBeginB.z || finestChunkBeginA.z == finestChunkEndB.z) && bOverlapX && bOverlapY; // z

	return bAdjacent;
}

u64 ErosionColumnKey(const VoxelChunkID& id)
{
	return (u64(u32(id.coord.x)) << 32) | u64(u32(id.coord.z));
}

} // namespace


// ---- Construction ---------------------------------------------------------

VoxelChunkRenderNode::VoxelChunkRenderNode(render::RenderDevice& rd)
	: Super(rd, "VoxelChunkPass")
{
	using namespace render;

	// Persistent geometry pages (no index buffer; the mesh-shader path consumes meshlets).
	m_pVertexPool = Buffer::Create(rd, "VoxelChunkPass::VertexPool",
		{
			.count              = VoxelTotalVertexPool(),
			.elementSizeInBytes = sizeof(VoxelVertex),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	m_pMeshletPool = Buffer::Create(rd, "VoxelChunkPass::MeshletPool",
		{
			.count              = VoxelTotalMeshletPool(),
			.elementSizeInBytes = sizeof(Meshlet),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	m_pMeshletVertexPool = Buffer::Create(rd, "VoxelChunkPass::MeshletVertexPool",
		{
			.count              = VoxelTotalMeshletVertexPool(),
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	m_pMeshletTrianglePool = Buffer::Create(rd, "VoxelChunkPass::MeshletTrianglePool",
		{
			.count              = VoxelTotalMeshletTriPool(),
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	m_pChunkCountsBuffer = Buffer::Create(rd, "VoxelChunkPass::ChunkCounts",
		{
			.count              = kMaxChunks,
			.elementSizeInBytes = sizeof(VoxelChunkCounts),
			.bufferUsage        = eBufferUsage_Storage,
		});
	m_pChunkDescBuffer = Buffer::Create(rd, "VoxelChunkPass::ChunkDescs",
		{
			.count              = kMaxChunks,
			.elementSizeInBytes = sizeof(VoxelChunkDesc),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	for (VoxelChunkDesc& desc : m_ChunkDescs)
		desc.pageID = kInvalidIndex;
	for (u32 c = 0u; c < kVoxelPageClassCount; ++c)
		m_FreePages[c].reserve(VoxelClassPageCount(c));
	m_FreeErosionSlices.reserve(kMaxVoxelErosionSlices);

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
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest | eBufferUsage_TransferSource,
		});
	m_pTriCountReadback = Buffer::Create(rd, "VoxelChunkPass::TriCountReadback",
		{
			.count              = kTriReadbackSlots * kTriReadbackPerFrame * kMCCounterFields,
			.elementSizeInBytes = sizeof(u32),
			.mapDirection       = 2,
			.bufferUsage        = eBufferUsage_TransferDest,
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
			.imageType   = eImageType::Texture2DArray,
			.resolution  = uint3(kErosionMapDim, kErosionMapDim, 1),
			.format      = eFormat::RGBA16_FLOAT,
			.imageUsage  = eTextureUsage_Storage | eTextureUsage_Sample,
			.arrayLayers = kMaxVoxelErosionSlices,
		});
	auto pErosionBakeCS = Shader::Create(rd, "VoxelErosionBakeCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelErosionBakeCS" });
	m_pErosionBakePSO = ComputePipeline::Create(rd, "VoxelErosionBakePSO");
	m_pErosionBakePSO->SetComputeShader(pErosionBakeCS).Build();

	g_FrameData.pVoxelChunkDescs       = m_pChunkDescBuffer;
	g_FrameData.pVoxelChunkCounts      = m_pChunkCountsBuffer;
	g_FrameData.pVoxelVertices         = m_pVertexPool;
	g_FrameData.pVoxelMeshlets         = m_pMeshletPool;
	g_FrameData.pVoxelMeshletVertices  = m_pMeshletVertexPool;
	g_FrameData.pVoxelMeshletTriangles = m_pMeshletTrianglePool;
	g_FrameData.pVoxelErosionDetail    = m_pErosionDetailMap;

	for (u32 i = 0; i < kMaxVoxelChunkSlots; ++i)
		m_ChunkSlots[i].chunkIndex = i;
}

// ---- Chunk residency ------------------------------------------------------

u32 VoxelChunkRenderNode::AllocatePage(u32 classId)
{
	if (!m_FreePages[classId].empty())
	{
		const u32 idx = m_FreePages[classId].back();
		m_FreePages[classId].pop_back();
		return MakeVoxelPageID(classId, idx);
	}
	if (m_NumAllocatedPages[classId] < VoxelClassPageCount(classId))
		return MakeVoxelPageID(classId, m_NumAllocatedPages[classId]++);

	return kInvalidIndex; // class pool exhausted
}

u32 VoxelChunkRenderNode::AllocatePageAtLeast(u32 classId)
{
	for (u32 c = classId; c < kVoxelPageClassCount; ++c)
	{
		const u32 pageID = AllocatePage(c);
		if (pageID != kInvalidIndex)
			return pageID;
	}
	return kInvalidIndex;
}

void VoxelChunkRenderNode::DeallocatePage(u32 pageID)
{
	if (pageID == kInvalidIndex)
		return;

	m_FreePages[VoxelPageClassId(pageID)].push_back(VoxelPageIdx(pageID));
}

void VoxelChunkRenderNode::RetireSlot(ChunkSlot& chunkSlot)
{
	const u32 slotIndex = chunkSlot.chunkIndex;
	DeallocatePage(chunkSlot.pageID);
	DeallocatePage(chunkSlot.pendingPageID);
	if (chunkSlot.id.lod == 0u)
		ReleaseErosionColumn(chunkSlot.id);

	chunkSlot = {};
	chunkSlot.chunkIndex = slotIndex;

	m_ChunkDescs[slotIndex] = {};
	m_ChunkDescs[slotIndex].pageID = kInvalidIndex;
}

u32 VoxelChunkRenderNode::AllocatePageOrReclaim(u32 classId, const float3& camPos, float baseChunkSizeMeter)
{
	const u32 pageID = AllocatePageAtLeast(classId);
	if (pageID != kInvalidIndex)
		return pageID;

	// reclaim the farthest retiring chunk holding a big-enough page; its built members go visible now (partial swap)
	ChunkSlot* victim = nullptr;
	float farthest = -1.0f;
	for (ChunkSlot& slot : m_ChunkSlots)
	{
		if (slot.state == eChunkState::Empty || slot.bDesired || !slot.bVisible)
			continue;

		const bool bReclaimable = 
			(slot.pageID != kInvalidIndex && VoxelPageClassId(slot.pageID) >= classId) || (slot.pendingPageID != kInvalidIndex && VoxelPageClassId(slot.pendingPageID) >= classId);
		if (!bReclaimable)
			continue;

		const float half = 0.5f * baseChunkSizeMeter * float(1u << slot.id.lod);
		const float dist = std::max(std::abs(camPos.x - (slot.originWS.x + half)), std::abs(camPos.z - (slot.originWS.z + half)));
		if (dist > farthest)
		{
			farthest = dist;
			victim   = &slot;
		}
	}
	if (victim == nullptr)
		return kInvalidIndex;

	for (ChunkSlot& member : m_ChunkSlots)
	{
		if (member.bDesired && IsBuilt(member) && IsChunksOverlapped(victim->id, member.id))
			member.bVisible = true;
	}
	RetireSlot(*victim);
	++m_ReclaimCount;

	return AllocatePageAtLeast(classId);
}

u32 VoxelChunkRenderNode::AllocateErosionSlice()
{
	if (!m_FreeErosionSlices.empty())
	{
		const u32 slice = m_FreeErosionSlices.back();
		m_FreeErosionSlices.pop_back();
		return slice;
	}
	if (m_NumAllocatedSlices < kMaxVoxelErosionSlices)
		return m_NumAllocatedSlices++;

	return kInvalidIndex;
}

void VoxelChunkRenderNode::FreeErosionSlice(u32 slice)
{
	if (slice == kInvalidIndex)
		return;

	m_FreeErosionSlices.push_back(slice);
}

u32 VoxelChunkRenderNode::AcquireErosionColumn(const VoxelChunkID& id)
{
	ErosionColumn& column = m_ErosionColumns[ErosionColumnKey(id)];
	if (column.refCount++ == 0u)
		column.slice = AllocateErosionSlice();

	return column.slice;
}

void VoxelChunkRenderNode::ReleaseErosionColumn(const VoxelChunkID& id)
{
	auto it = m_ErosionColumns.find(ErosionColumnKey(id));
	if (it == m_ErosionColumns.end())
		return;

	if (--it->second.refCount == 0u)
	{
		FreeErosionSlice(it->second.slice);
		m_ErosionColumns.erase(it);
	}
}

bool VoxelChunkRenderNode::ConsumeErosionBake(const VoxelChunkID& id, u32 revision)
{
	auto it = m_ErosionColumns.find(ErosionColumnKey(id));
	if (it == m_ErosionColumns.end() || it->second.slice == kInvalidIndex || it->second.bakedRevision == revision)
		return false;

	it->second.bakedRevision = revision;
	return true;
}

u32 VoxelChunkRenderNode::SelectPageClass(const ChunkSlot& chunkSlot) const
{
	if (chunkSlot.lastTriCount <= 0u)
		return kVoxelInitialClassId;

	u32 reservedTriCount = u32(glm::round(float(chunkSlot.lastTriCount) * kVoxelPageClassTriangleReserveRate));
	for (u32 i = 0; i < kVoxelPageClassCount; ++i)
	{
		if (reservedTriCount <= VoxelClassTriCap(i))
			return i;
	}

	return 3u; // XL
}

void VoxelChunkRenderNode::InstallPage(ChunkSlot& chunkSlot, u32 pageID)
{
	chunkSlot.pageID = pageID;

	const u32 c   = VoxelPageClassId(pageID);
	const u32 idx = VoxelPageIdx(pageID);

	VoxelChunkDesc& desc = m_ChunkDescs[chunkSlot.chunkIndex];
	desc.vOffset  = VoxelClassVertexBase(c) + idx * VoxelClassVertexCap(c);
	desc.mvOffset = VoxelClassMeshletVertexBase(c) + idx * VoxelClassMeshletVertexCap(c);
	desc.mtOffset = VoxelClassMeshletTriBase(c) + idx * VoxelClassMeshletTriCap(c);
	desc.mOffset  = VoxelClassMeshletBase(c) + idx * VoxelClassMeshletCap(c);
	desc.pageID   = pageID;
}

void VoxelChunkRenderNode::EnsurePageStatics(render::CommandContext& context, u32 pageID)
{
	if (!m_bTriTableUploaded)
	{
		std::vector< i32 > triTable(MarchingCubes::kFlatTriangleTableSize);
		MarchingCubes::FillFlatTriangleTable(triTable.data());
		context.UploadData(m_pMCTriTable, triTable.data(), MarchingCubes::kFlatTriangleTableSize, sizeof(i32), 0);
		m_bTriTableUploaded = true;
	}

	// Per-corner meshlets are field-independent: vertices = identity, triangles = repeating {3t,3t+1,3t+2}. Upload once per page.
	const u32 c           = VoxelPageClassId(pageID);
	const u32 idx         = VoxelPageIdx(pageID);
	const u32 pageOrdinal = VoxelClassPageBase(c) + idx;
	if (m_PageStaticUploaded[pageOrdinal])
		return;

	const u32 mvBase = VoxelClassMeshletVertexBase(c) + idx * VoxelClassMeshletVertexCap(c);
	const u32 mtBase = VoxelClassMeshletTriBase(c) + idx * VoxelClassMeshletTriCap(c);

	std::vector< u32 > identityMV(VoxelClassMeshletVertexCap(c));
	for (u32 i = 0u; i < VoxelClassMeshletVertexCap(c); ++i)
		identityMV[i] = i;

	std::vector< u32 > patternMT(VoxelClassMeshletTriCap(c));
	for (u32 p = 0u; p < VoxelClassMeshletTriCap(c); ++p)
	{
		const u32 t = p % kTrianglesPerMeshlet;
		patternMT[p] = ((3u * t + 2u) << 16) | ((3u * t + 1u) << 8) | (3u * t);
	}

	context.UploadData(m_pMeshletVertexPool, identityMV.data(), VoxelClassMeshletVertexCap(c), sizeof(u32), (u64)mvBase * sizeof(u32));
	context.UploadData(m_pMeshletTrianglePool, patternMT.data(),  VoxelClassMeshletTriCap(c),    sizeof(u32), (u64)mtBase * sizeof(u32));
	m_PageStaticUploaded[pageOrdinal] = true;
}

bool VoxelChunkRenderNode::EnsureChunkResident(render::CommandContext& context, const VoxelTerrainGenParams& gp, ChunkSlot& chunkSlot, const float3& camPos, float baseChunkSizeMeter)
{
	if (chunkSlot.pageID == kInvalidIndex)
	{
		const u32 targetClass = SelectPageClass(chunkSlot);
		chunkSlot.pageID = AllocatePageOrReclaim(targetClass, camPos, baseChunkSizeMeter);
		if (chunkSlot.pageID == kInvalidIndex)
		{
			++m_AllocFailCount;
			if ((m_AllocFailCount & (m_AllocFailCount - 1u)) == 0u)
				fprintf(stderr, "[VoxelChunkRenderNode] page pool exhausted (class %u, fail #%u).\n", targetClass, m_AllocFailCount);
			return false;
		}
	}
	EnsurePageStatics(context, chunkSlot.pageID);

	VoxelChunkDesc desc = {};
	desc.originWS       = float3(int3(gp.chunkCoordX, gp.chunkCoordY, gp.chunkCoordZ) * (i32)gp.cellsPerAxis) * gp.voxelSizeMeter;
	desc.chunkSizeMeter = float(gp.cellsPerAxis) * gp.voxelSizeMeter;
	desc.voxelSizeMeter = gp.voxelSizeMeter;
	desc.lodAndMask     = chunkSlot.id.lod & 0xFFu; // mask byte lands here at swap time (G-4)
	desc.erosionSlice   = chunkSlot.erosionSlice;
	desc.flags          = 0u; // build only; no-render
	m_ChunkDescs[chunkSlot.chunkIndex] = desc;

	InstallPage(chunkSlot, chunkSlot.pageID);

	return true;
}

// ---- GPU dispatch passes ----------------------------------------------------

// This pass only draws the height field on XZ plane so it currently wastes y-samples.
// TODO: [2-pass] 1) compute height field on XZ plane, 2) compute density by sampling the height field
void VoxelChunkRenderNode::DispatchDensity(render::CommandContext& context, const VoxelTerrainGenParams& gp, const ChunkSlot& chunkSlot)
{
	using namespace render;
	if (!m_pDensityPSO || !m_pDensityVolume)
		return;

	const u32 dim = gp.samplesPerAxis + 2u * gp.apron; // C+1+2A
	if (dim == 0u || dim > kDensityVolumeDim)
	{
		fprintf(stderr, "[VoxelDensity] dim %u exceeds volume %u -- density skipped.\n", dim, kDensityVolumeDim);
		return;
	}

	BAAMBOO_GPU_SCOPE(context, "Density");

	context.SetRenderPipeline(m_pDensityPSO.get());

	context.TransitionBarrier(m_pDensityVolume, eTextureLayout::General);
	context.TransitionBufferToWrite(m_pDensityField, ePipelineStage::ComputeShader);

	context.SetComputeDynamicUniformBuffer("g_VoxelGenParams", gp);
	context.StageDescriptor("g_OutDensityTex", m_pDensityVolume);
	context.StageDescriptor("g_OutDensityDebug", m_pDensityField);

	context.Dispatch3D< 4, 4, 4 >(dim, dim, dim);

	context.TransitionBarrier(m_pDensityVolume, eTextureLayout::ShaderReadOnly);
}

void VoxelChunkRenderNode::DispatchExtraction(render::CommandContext& context, const VoxelTerrainGenParams& gp, const ChunkSlot& chunkSlot, u32 buildPageID)
{
	using namespace render;
	if (!m_pMCExtractPSO || !m_pMeshletBuildPSO || buildPageID == kInvalidIndex)
		return;

	const u32 C      = gp.cellsPerAxis;
	const u32 apron  = gp.apron;
	const u32 cls    = VoxelPageClassId(buildPageID);
	const u32 idx    = VoxelPageIdx(buildPageID);
	const u32 triCap = VoxelClassTriCap(cls);
	const u32 vBase  = VoxelClassVertexBase(cls) + idx * VoxelClassVertexCap(cls);
	const u32 mlBase = VoxelClassMeshletBase(cls) + idx * VoxelClassMeshletCap(cls);
	if (C == 0u)
		return;

	// Pass A: marching cubes -- each active cell atomic-appends its per-corner triangle vertices to the page.
	context.BeginGpuMarker("MCExtract");
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
		u32   vertexPageBase;
		u32   maxTriangles;
	} mc = { C, apron, gp.voxelSizeMeter, vBase, triCap };
	context.SetComputeConstants(sizeof(mc), &mc);
	context.StageDescriptor("g_TriTable", m_pMCTriTable);
	context.StageDescriptor("g_DensityField", m_pDensityField);
	context.StageDescriptor("g_MCCounter", m_pMCCounter);
	context.StageDescriptor("g_OutVertices", m_pVertexPool);
	context.Dispatch3D< 4, 4, 4 >(C, C, C);
	context.EndGpuMarker();

	context.UAVBarrier(m_pMCCounter, true); // extract -> sort
	context.UAVBarrier(m_pVertexPool);

	// Pass A2: triangle spatial sort into Morton blocks, baked into the meshlet-vertex indirection (vertices stay in place).
	const u32 mvBase = VoxelClassMeshletVertexBase(cls) + idx * VoxelClassMeshletVertexCap(cls);

	context.BeginGpuMarker("TriSortCount");
	context.ClearBuffer(m_pTriSortBins, 0u);

	context.TransitionBufferToRead(m_pVertexPool, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pTriSortBins, ePipelineStage::ComputeShader);

	struct
	{
		u32   vertexPageBase;
		u32   meshletVertexPageBase;
		u32   maxTriangles;
		float chunkSizeMeter;
	} ts = { vBase, mvBase, triCap, float(C) * gp.voxelSizeMeter };

	// A2.1: histogram
	context.SetRenderPipeline(m_pTriSortCountPSO.get());
	context.SetComputeConstants(sizeof(ts), &ts);
	context.StageDescriptor("g_MCCounter", m_pMCCounter);
	context.StageDescriptor("g_Vertices", m_pVertexPool);
	context.StageDescriptor("g_SortBins", m_pTriSortBins);
	context.Dispatch1D< 256 >(triCap);
	context.EndGpuMarker();

	context.UAVBarrier(m_pTriSortBins, true);

	// A2.2: exclusive scan (single group)
	static_assert(kTriSortBins % 1024u == 0u, "scan CS strips assume bins % threads == 0");
	context.BeginGpuMarker("TriSortScan");
	context.SetRenderPipeline(m_pTriSortScanPSO.get());
	struct { u32 numBins; } sc = { kTriSortBins };
	context.SetComputeConstants(sizeof(sc), &sc);
	context.StageDescriptor("g_SortBins", m_pTriSortBins);
	context.Dispatch1D< 1024 >(1024u);
	context.EndGpuMarker();

	context.UAVBarrier(m_pTriSortBins, true);

	// A2.3: scatter the permutation into the meshlet-vertex indirection
	context.BeginGpuMarker("TriSortScatter");
	context.TransitionBufferToWrite(m_pMeshletVertexPool, ePipelineStage::ComputeShader);

	context.SetRenderPipeline(m_pTriSortScatterPSO.get());
	context.SetComputeConstants(sizeof(ts), &ts);
	context.StageDescriptor("g_MCCounter", m_pMCCounter);
	context.StageDescriptor("g_Vertices", m_pVertexPool);
	context.StageDescriptor("g_SortBins", m_pTriSortBins);
	context.StageDescriptor("g_OutMeshletVerts", m_pMeshletVertexPool);
	context.Dispatch1D< 256 >(triCap);
	context.EndGpuMarker();

	context.UAVBarrier(m_pMeshletVertexPool, true); // sort -> meshlet build

	// Pass B: pack sequential meshlets (+ bounds through the sorted indirection) and patch counts.
	context.BeginGpuMarker("MeshletBuild");
	context.TransitionBufferToWrite(m_pMeshletPool, ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pChunkCountsBuffer, ePipelineStage::ComputeShader);
	context.TransitionBufferToRead(m_pMeshletVertexPool, ePipelineStage::ComputeShader);

	context.SetRenderPipeline(m_pMeshletBuildPSO.get());
	struct
	{
		u32 chunkID;
		u32 meshletPageBase;
		u32 trianglesPerMeshlet;
		u32 maxMeshlets;
		u32 maxTriangles;
		u32 vertexPageBase;
		u32 meshletVertexPageBase;
		float chunkSizeMeter;
	} mb = { chunkSlot.chunkIndex, mlBase, kTrianglesPerMeshlet, VoxelClassMeshletCap(cls), triCap, vBase, mvBase, float(C) * gp.voxelSizeMeter };
	context.SetComputeConstants(sizeof(mb), &mb);
	context.StageDescriptor("g_MCCounter", m_pMCCounter);
	context.StageDescriptor("g_OutMeshlets", m_pMeshletPool);
	context.StageDescriptor("g_OutCounts", m_pChunkCountsBuffer);
	context.StageDescriptor("g_Vertices", m_pVertexPool);
	context.StageDescriptor("g_MeshletVerts", m_pMeshletVertexPool);
	context.Dispatch1D< 64 >(VoxelClassMeshletCap(cls));
	context.EndGpuMarker();

	context.UAVBarrier(m_pMeshletPool);
	context.UAVBarrier(m_pChunkCountsBuffer, true);
}

void VoxelChunkRenderNode::DispatchErosionBake(render::CommandContext& context, const VoxelTerrainGenParams& gp, const ChunkSlot& chunkSlot)
{
	using namespace render;
	if (!m_pErosionBakePSO || !m_pErosionDetailMap || chunkSlot.erosionSlice == kInvalidIndex)
		return;

	BAAMBOO_GPU_SCOPE(context, "ErosionBake");

	context.SetRenderPipeline(m_pErosionBakePSO.get());

	context.TransitionBarrier(m_pErosionDetailMap, eTextureLayout::General);

	context.SetComputeDynamicUniformBuffer("g_VoxelGenParams", gp);

	struct
	{
		u32 erosionSlice;
	} constant = { chunkSlot.erosionSlice };
	context.SetComputeConstants(sizeof(constant), &constant);

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

	PublishTriReadback();
	
	if (vt.bValid)
	{
		m_CurrentRevision = vt.revision;
		m_RecenterCount   = vt.recenterCount;
	}

	for (ChunkSlot& slot : m_ChunkSlots)
		slot.bDesired = false;

	std::unordered_map< u64, u32 > desiredChunksToAdd;
	for (const VoxelChunkView& view : vt.chunks)
	{
		u64 targetKey = VoxelChunkKey(view.id);
		auto resident = std::ranges::find_if(m_ChunkSlots.begin(), m_ChunkSlots.end(),
			[&view, targetKey](const ChunkSlot& slot)
				{
					return slot.state != eChunkState::Empty && VoxelChunkKey(slot.id) == targetKey;
				});
		// allocate(queueing) if not resident
		if (resident == m_ChunkSlots.end())
		{
			auto empty = std::ranges::find_if(m_ChunkSlots.begin(), m_ChunkSlots.end(), [](const ChunkSlot& slot) { return slot.state == eChunkState::Empty; });
			if (empty == m_ChunkSlots.end())
			{
				fprintf(stderr, "[VoxelChunkRenderNode] chunk slot pool exhausted.\n");
				break;
			}

			empty->id       = view.id;
			empty->originWS = view.originWS;
			empty->state    = eChunkState::Queued;
			empty->bDesired = true;
			if (view.id.lod == 0u)
				empty->erosionSlice = AcquireErosionColumn(view.id);

			desiredChunksToAdd.emplace(targetKey, empty->chunkIndex);
		}
		else
		{
			if (vt.bValid && (resident->state == eChunkState::Resident || resident->state == eChunkState::ResidentEmpty)
				&& resident->builtRevision != vt.revision && resident->pendingRevision != vt.revision && resident->rejectedRevision != vt.revision)
				resident->state = eChunkState::Dirty;

			resident->bDesired = true;

			desiredChunksToAdd.emplace(targetKey, resident->chunkIndex);
		}
	}

	// ready swap groups collected this frame; committed together after the 2:1 gate verdict
#ifdef _DEBUG
	// I-BAL: the cut itself must never demand face-adjacent chunks more than one level apart
	for (size_t a = 0; a < vt.chunks.size(); ++a)
		for (size_t b = a + 1; b < vt.chunks.size(); ++b)
		{
			const int lodDelta = int(vt.chunks[a].id.lod) - int(vt.chunks[b].id.lod);
			if ((lodDelta >= 2 || lodDelta <= -2) && AreChunksFaceAdjacent(vt.chunks[a].id, vt.chunks[b].id))
				BB_ASSERT(false, "I-BAL violated in the desired cut: L%u vs L%u", vt.chunks[a].id.lod, vt.chunks[b].id.lod);
		}
#endif

	struct SwapCandidate { u32 retireSlot; std::vector< u32 > replacementSlots; };
	std::vector< SwapCandidate > candidates;

	for (ChunkSlot& slot : m_ChunkSlots)
	{
		if (slot.state == eChunkState::Empty)
			continue;

		// new chunk visible; only if all overlapping chunks are evicted
		if (slot.bDesired && !slot.bVisible && IsBuilt(slot))
		{
			bool bOverlaps = false;
			for (const ChunkSlot& s : m_ChunkSlots)
			{
				if (!s.bVisible) 
					continue;

				if (IsChunksOverlapped(slot.id, s.id))
				{
					bOverlaps = true;
					break;
				}
			}

			if (!bOverlaps)
				slot.bVisible = true;
		}

		// old chunk eviction; judged by the 2:1 gate below once all replacements are built
		if (slot.bVisible && !slot.bDesired)
		{
			std::vector< u32 > replacementSlots;

			bool bAllBuilt = true;
			for (const auto& e : desiredChunksToAdd)
			{
				const ChunkSlot& replacement = m_ChunkSlots[e.second];
				if (!IsChunksOverlapped(slot.id, replacement.id))
					continue;

				if (!IsBuilt(replacement))
				{
					bAllBuilt = false;
					break;
				}

				replacementSlots.push_back(e.second);
			}

			// register as candidate to evict current slot and make-visible replacements if all overlapping chunks(=replacements) are built
			if (bAllBuilt)
			{
				SwapCandidate& candidate = candidates.emplace_back();
				candidate.retireSlot       = slot.chunkIndex;
				candidate.replacementSlots = std::move(replacementSlots);
			}
		}
		else if (!slot.bDesired && !slot.bVisible)
		{
			RetireSlot(slot);
		}
	}

	// 2:1 gate
	std::vector< bool > alive(candidates.size(), true);

	u32  numHeldSwaps = 0u;
	bool bPruned      = true;
	while (bPruned)
	{
		bPruned = false;
		std::vector< bool > retires(m_ChunkSlots.size(), false);
		std::vector< bool > enters(m_ChunkSlots.size(), false);

		for (size_t i = 0; i < candidates.size(); ++i)
		{
			if (!alive[i])
				continue;

			retires[candidates[i].retireSlot] = true;
			for (u32 replacementSlot : candidates[i].replacementSlots)
				enters[replacementSlot] = true;
		}

		for (size_t i = 0; i < candidates.size(); ++i)
		{
			if (!alive[i])
				continue;

			bool bViolates = false;
			for (u32 replacementSlot : candidates[i].replacementSlots)
			{
				const ChunkSlot& replacement = m_ChunkSlots[replacementSlot];
				if (replacement.state == eChunkState::ResidentEmpty)
					continue;

				for (const ChunkSlot& s : m_ChunkSlots)
				{
					if (s.state == eChunkState::Empty || s.state == eChunkState::ResidentEmpty || s.chunkIndex == replacementSlot)
						continue;

					const bool bVisibleAfterCommit = (s.bVisible && !retires[s.chunkIndex]) || enters[s.chunkIndex];
					if (!bVisibleAfterCommit)
						continue;

					const int lodDelta = int(replacement.id.lod) - int(s.id.lod);
					if (lodDelta > -2 && lodDelta < 2)
						continue;

					if (AreChunksFaceAdjacent(replacement.id, s.id))
					{
						bViolates = true;
						break;
					}
				}

				if (bViolates)
					break;
			}

			if (bViolates)
			{
				alive[i] = false;
				bPruned  = true;
				++numHeldSwaps;
			}
		}
	}
	m_NumHeldSwaps = numHeldSwaps;

	for (size_t i = 0; i < candidates.size(); ++i)
	{
		if (!alive[i])
			continue;

		RetireSlot(m_ChunkSlots[candidates[i].retireSlot]);
		for (u32 replacementSlot : candidates[i].replacementSlots)
			m_ChunkSlots[replacementSlot].bVisible = true;
	}

	std::vector< u32 > markedChunks;
	for (const ChunkSlot& slot : m_ChunkSlots)
	{
		// retiring slots keep rendering their last geometry; only cut members earn build budget
		if (slot.bDesired && (slot.state == eChunkState::Queued || slot.state == eChunkState::Dirty))
			markedChunks.push_back(slot.chunkIndex);
	}
	std::ranges::sort(markedChunks.begin(), markedChunks.end(), [&](u32 a, u32 b)
		{
			const auto& slotA = m_ChunkSlots[a];
			const auto& slotB = m_ChunkSlots[b];

			const float halfA = 0.5f * vt.chunkWorldSizeMeter * float(1u << slotA.id.lod);
			const float halfB = 0.5f * vt.chunkWorldSizeMeter * float(1u << slotB.id.lod);
			float dA = std::max(std::abs(renderView.camera.pos.x - (slotA.originWS.x + halfA)), std::abs(renderView.camera.pos.z - (slotA.originWS.z + halfA)));
			float dB = std::max(std::abs(renderView.camera.pos.x - (slotB.originWS.x + halfB)), std::abs(renderView.camera.pos.z - (slotB.originWS.z + halfB)));
			return dA < dB;
		});

	static constexpr u32 kMaxChunkBuildsPerFrame = 1;
	u32 numBuilt = 0u;
	for (u32 i = 0; i < static_cast<u32>(markedChunks.size()) && numBuilt < kMaxChunkBuildsPerFrame; ++i)
	{
		ChunkSlot& slot = m_ChunkSlots[markedChunks[i]];

		VoxelTerrainGenParams gp = vt.genParams;
		gp.chunkCoordX = slot.id.coord.x;
		gp.chunkCoordY = slot.id.coord.y;
		gp.chunkCoordZ = slot.id.coord.z;
		gp.voxelSizeMeter *= float(1u << slot.id.lod); // LOD-k chunk: same 128^3 grid, doubled cell size per level

		// GPU build: density volume -> marching cubes -> vertex/meshlet pools + row count patch.
		u32 buildPageID = kInvalidIndex;

		const bool bRebuild = (slot.pageID != kInvalidIndex);
		if (!bRebuild)
		{
			if (!EnsureChunkResident(context, gp, slot, renderView.camera.pos, vt.chunkWorldSizeMeter))
				continue;

			buildPageID = slot.pageID;
		}
		else
		{
			const u32 targetClass = SelectPageClass(slot);
			if (slot.rejectedRevision == vt.revision && targetClass <= slot.rejectedClassId)
			{
				slot.state = eChunkState::Resident;
				continue;
			}

			buildPageID = AllocatePageOrReclaim(targetClass, renderView.camera.pos, vt.chunkWorldSizeMeter);
			if (buildPageID == kInvalidIndex)
			{
				++m_AllocFailCount;
				if ((m_AllocFailCount & (m_AllocFailCount - 1u)) == 0u) // exponential backoff: log on fail #1, 2, 4, 8, ...
					fprintf(stderr, "[VoxelChunkRenderNode] pending page alloc failed (class %u, fail #%u).\n", targetClass, m_AllocFailCount);
				continue;
			}
			EnsurePageStatics(context, buildPageID);

			if (slot.pendingPageID != kInvalidIndex)
				DeallocatePage(slot.pendingPageID);

			slot.pendingPageID   = buildPageID;
			slot.pendingRevision = vt.revision;
		}

		u32 frameBuildCount = 0u;
		{
			BAAMBOO_GPU_SCOPE(context, "VoxelChunkBuild");

			DispatchDensity(context, gp, slot);
			DispatchExtraction(context, gp, slot, buildPageID);
			if (ConsumeErosionBake(slot.id, vt.revision)) // once per XZ column per revision; Y layers share the map
				DispatchErosionBake(context, gp, slot);

			if (frameBuildCount < kTriReadbackPerFrame)
			{
				const u32 entry = m_TriReadbackIdx * kTriReadbackPerFrame + frameBuildCount;
				context.CopyBufferRegion(m_pTriCountReadback, m_pMCCounter, kMCCounterFields * sizeof(u32), (u64)entry * kMCCounterFields * sizeof(u32), 0);
				m_TriReadbackTags[entry] = { slot.chunkIndex, vt.revision, VoxelClassTriCap(VoxelPageClassId(buildPageID)) };
				++frameBuildCount;
			}

			slot.state = eChunkState::Resident;
			if (!bRebuild)
				slot.builtRevision = vt.revision;

			++numBuilt;
		}
	}

	if (vt.bValid)
	{
		const CameraRenderView& cam = renderView.bFrozen ? renderView.frozenCamera : renderView.camera;

		const float2 viewport = renderView.bFrozen ? renderView.frozenViewport : renderView.viewport;
		const float kScale = 0.5f * viewport.y * std::abs(cam.mProj[1][1]);

		for (u32 i = 0u; i < kMaxVoxelChunkSlots; ++i)
		{
			VoxelChunkDesc& desc = m_ChunkDescs[i];
			desc.diceMaxLevel          = (m_ChunkSlots[i].id.lod == 0u) ? vt.dice.maxLevel : 0u; // dicing is a LOD0-only band
			desc.diceTargetPx          = vt.dice.targetPx;
			desc.diceRadiusMeter       = vt.dice.radiusM;
			desc.diceFadeWidthMeter    = vt.dice.fadeWidthMeter;
			desc.diceDisplacementScale = vt.dice.displacementScale;
			desc.debugFlags            = vt.dice.debugFlags;
			desc.diceKScale            = kScale;

			desc.microAmplitudeMeter      = vt.dice.microAmplitudeMeter;
			desc.microBaseWaveLengthMeter = vt.dice.microBaseWaveLengthMeter;
			desc.microLacunarity          = vt.dice.microLacunarity;
			desc.microGain                = vt.dice.microGain;
			desc.microCreaseBoost         = vt.dice.microCreaseBoost;
			desc.microSharpness           = vt.dice.microSharpness;
			desc.microOctaves             = vt.dice.microOctaves;

			desc.flags = m_ChunkSlots[i].bVisible ? 1u : 0u;
		}

		context.UploadData(m_pChunkDescBuffer, m_ChunkDescs.data(), kMaxVoxelChunkSlots, sizeof(VoxelChunkDesc), 0);
	}

	m_TriReadbackIdx = (m_TriReadbackIdx + 1u) % kTriReadbackSlots;
	if (m_TriReadbackFrameCounter <= kTriReadbackSlots)
		++m_TriReadbackFrameCounter;
}

void VoxelChunkRenderNode::PublishTriReadback()
{
	if (m_TriReadbackFrameCounter < kTriReadbackSlots)
		return;

	for (u32 i = 0u; i < kTriReadbackPerFrame; ++i)
	{
		const u32 entry = m_TriReadbackIdx * kTriReadbackPerFrame + i;

		TriReadbackTag& tag = m_TriReadbackTags[entry];
		if (tag.chunkIndex == kInvalidIndex)
			continue;

		const u64 offset = (u64)entry * kMCCounterFields * sizeof(u32);
		m_pTriCountReadback->InvalidateMappedRange(offset, kMCCounterFields * sizeof(u32));
		if (auto* counts = static_cast< u32* >(m_pTriCountReadback->MappedMemory()))
		{
			ChunkSlot& slot = m_ChunkSlots[tag.chunkIndex];

			const bool bPendingVerdict = slot.state != eChunkState::Empty && slot.pendingPageID != kInvalidIndex && slot.pendingRevision == tag.revision;
			const bool bLiveVerdict    = slot.state != eChunkState::Empty && slot.builtRevision == tag.revision;
			if (bPendingVerdict || bLiveVerdict)
			{
				slot.lastTriCount    = counts[entry * kMCCounterFields + 0u];
				slot.lastTriRevision = tag.revision;

				m_LastBuildChunkIndex = tag.chunkIndex;
				m_LastBuildTriCount   = slot.lastTriCount;
				m_LastBuildCellCount  = counts[entry * kMCCounterFields + 1u];
				m_LastBuildTriCap     = tag.triCap;
			}

			const u32 cellCount = counts[entry * kMCCounterFields + 1u];
			if (bPendingVerdict)
			{
				if (slot.lastTriCount <= tag.triCap)
				{
					DeallocatePage(slot.pageID);
					if (cellCount == 0u)
					{
						DeallocatePage(slot.pendingPageID);
						slot.pageID = kInvalidIndex;
						slot.state  = eChunkState::ResidentEmpty;
						m_ChunkDescs[tag.chunkIndex].pageID = kInvalidIndex;
					}
					else
					{
						InstallPage(slot, slot.pendingPageID);
					}
					slot.builtRevision = tag.revision;
				}
				else
				{
					DeallocatePage(slot.pendingPageID);
					slot.rejectedRevision = tag.revision;
					slot.rejectedClassId  = VoxelPageClassId(slot.pendingPageID);
					if (slot.rejectedClassId + 1u < kVoxelPageClassCount)
						slot.state = eChunkState::Dirty; // requeue -- SelectPageClass must exceed rejectedClassId
				}
				slot.pendingPageID   = kInvalidIndex;
				slot.pendingRevision = kInvalidIndex;
			}
			else if (bLiveVerdict && slot.lastTriCount > tag.triCap)
			{
				slot.rejectedRevision = tag.revision;
				slot.rejectedClassId  = VoxelPageClassId(slot.pageID);
				if (slot.rejectedClassId + 1u < kVoxelPageClassCount)
					slot.state = eChunkState::Dirty; // clamped initial build -- rebuild one class up
			}
			else if (bLiveVerdict && cellCount == 0u)
			{
				DeallocatePage(slot.pageID);
				slot.pageID = kInvalidIndex;
				slot.state  = eChunkState::ResidentEmpty;
				m_ChunkDescs[tag.chunkIndex].pageID = kInvalidIndex;
			}
		}
		tag = {};
	}
}

void VoxelChunkRenderNode::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(width);
	UNUSED(height);
	UNUSED(depth);
}

void VoxelChunkRenderNode::DrawUI()
{
	if (ImGui::Begin("Voxel Streaming"))
	{
		static const char* kClassNames[] = { "S", "M", "L", "XL" };

		u32 numResident = 0, numQueued = 0, numDirty = 0, numEmpty = 0;
		u32 numMappedPerClass[kVoxelPageClassCount] = {};
		u32 numPerLevel[8] = {}, numDesiredPerLevel[8] = {}, numVisiblePerLevel[8] = {}, numEmptyPerLevel[8] = {};
		for (const ChunkSlot& slot : m_ChunkSlots)
		{
			switch (slot.state)
			{
			case eChunkState::Resident:      ++numResident; break;
			case eChunkState::Queued:        ++numQueued;   break;
			case eChunkState::Dirty:         ++numDirty;    break;
			case eChunkState::ResidentEmpty: ++numEmpty;    break;
			default: break;
			}
			if (slot.state != eChunkState::Empty)
			{
				const u32 level = std::min(slot.id.lod, 7u);
				++numPerLevel[level];
				if (slot.bDesired) ++numDesiredPerLevel[level];
				if (slot.bVisible) ++numVisiblePerLevel[level];
				if (slot.state == eChunkState::ResidentEmpty) ++numEmptyPerLevel[level];
			}
			if (slot.pageID != kInvalidIndex)
				++numMappedPerClass[VoxelPageClassId(slot.pageID)];
			if (slot.pendingPageID != kInvalidIndex)
				++numMappedPerClass[VoxelPageClassId(slot.pendingPageID)];
		}

		// 2-step visible seams: expected only while the pressure valve is force-swapping
		u32 numVisibleSeamViolations = 0u;
		for (u32 a = 0; a < (u32)m_ChunkSlots.size(); ++a)
		{
			const ChunkSlot& sa = m_ChunkSlots[a];
			if (sa.state == eChunkState::Empty || sa.state == eChunkState::ResidentEmpty || !sa.bVisible)
				continue;

			for (u32 b = a + 1u; b < (u32)m_ChunkSlots.size(); ++b)
			{
				const ChunkSlot& sb = m_ChunkSlots[b];
				if (sb.state == eChunkState::Empty || sb.state == eChunkState::ResidentEmpty || !sb.bVisible)
					continue;

				const int lodDelta = int(sa.id.lod) - int(sb.id.lod);
				if ((lodDelta >= 2 || lodDelta <= -2) && AreChunksFaceAdjacent(sa.id, sb.id))
					++numVisibleSeamViolations;
			}
		}

		for (u32 c = 0u; c < kVoxelPageClassCount; ++c)
		{
			const u32 numFree   = (u32)m_FreePages[c].size();
			const u32 numMapped = numMappedPerClass[c];
			if (numFree + numMapped == m_NumAllocatedPages[c])
				ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%-2s pages: %2u free + %2u mapped == %2u activated (cap %2u, %uk tris)",
					kClassNames[c], numFree, numMapped, m_NumAllocatedPages[c], VoxelClassPageCount(c), VoxelClassTriCap(c) / 1000u);
			else
				ImGui::TextColored(ImVec4(0.95f, 0.3f, 0.3f, 1.0f), "%-2s pages CONSERVATION VIOLATED: %u free + %u mapped != %u activated",
					kClassNames[c], numFree, numMapped, m_NumAllocatedPages[c]);
		}
		ImGui::Text("erosion slices: %u free / %u activated (cap %u) | %u columns", (u32)m_FreeErosionSlices.size(), m_NumAllocatedSlices, kMaxVoxelErosionSlices, (u32)m_ErosionColumns.size());
		ImGui::Text("slots: %u resident / %u empty / %u queued / %u dirty | recenters %u", numResident, numEmpty, numQueued, numDirty, m_RecenterCount);
		ImGui::Text("levels: L0 %u | L1 %u | L2 %u | L3 %u | L4+ %u",
			numPerLevel[0], numPerLevel[1], numPerLevel[2], numPerLevel[3],
			numPerLevel[4] + numPerLevel[5] + numPerLevel[6] + numPerLevel[7]);
		if (ImGui::TreeNode("Level stats"))
		{
			for (u32 level = 0u; level < 8u; ++level)
			{
				if (numPerLevel[level] == 0u)
					continue;

				ImGui::Text("L%u: %u slots | %u desired | %u visible | %u empty", level,
					numPerLevel[level], numDesiredPerLevel[level], numVisiblePerLevel[level], numEmptyPerLevel[level]);
			}
			ImGui::TreePop();
		}
		if (numVisibleSeamViolations != 0u)
			ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.0f), "visible 2-step seams: %u", numVisibleSeamViolations);
		if (m_ReclaimCount != 0u)
			ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.3f, 1.0f), "page reclaims (pressure valve): %u", m_ReclaimCount);
		if (m_NumHeldSwaps != 0u)
			ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.3f, 1.0f), "held swaps (2:1 gate): %u", m_NumHeldSwaps);
		if (m_AllocFailCount != 0u)
			ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.0f), "page alloc fails: %u", m_AllocFailCount);
		if (m_LastBuildChunkIndex != kInvalidIndex)
			ImGui::Text("last build: slot %u  tris %u / cap %u  cells %u", m_LastBuildChunkIndex, m_LastBuildTriCount, m_LastBuildTriCap, m_LastBuildCellCount);

		if (ImGui::TreeNode("Tri histogram"))
		{
			std::vector< u32 > samples;
			samples.reserve(kMaxVoxelChunkSlots);
			u32 numRejected = 0u;
			for (const ChunkSlot& slot : m_ChunkSlots)
			{
				if (slot.state == eChunkState::Empty || slot.lastTriRevision != m_CurrentRevision)
					continue;
				samples.push_back(slot.lastTriCount);
				if (slot.rejectedRevision == m_CurrentRevision)
					++numRejected;
			}

			if (samples.empty())
				ImGui::TextDisabled("no readback samples yet");
			else
			{
				std::ranges::sort(samples.begin(), samples.end());
				auto pct = [&](float p) { return samples[(size_t)(p * float(samples.size() - 1u))]; };
				ImGui::Text("samples %u  rejected %u", (u32)samples.size(), numRejected);
				ImGui::Text("p50 %u  p90 %u  p99 %u  max %u", pct(0.50f), pct(0.90f), pct(0.99f), samples.back());

				constexpr u32 kBins = 32u;
				float bins[kBins] = {};
				for (u32 tri : samples)
					bins[std::min(kBins - 1u, tri * kBins / kMaxTrianglesPerChunk)] += 1.0f;
				ImGui::PlotHistogram("##tribins", bins, (int)kBins, 0, "lastTri distribution (0 .. cap)", FLT_MAX, FLT_MAX, ImVec2(0.0f, 80.0f));

				static char sweepLabel[64] = "default";
				ImGui::SetNextItemWidth(160.0f);
				ImGui::InputText("label", sweepLabel, sizeof(sweepLabel));
				ImGui::SameLine();
				if (ImGui::Button("Dump CSV"))
				{
					std::ofstream csv(std::string("voxel_tri_stats_") + sweepLabel + ".csv", std::ios::trunc);
					csv << "chunkIndex,coordX,coordZ,lastTriCount,lastTriRevision,rejectedRevision\n";
					for (const ChunkSlot& slot : m_ChunkSlots)
					{
						if (slot.state == eChunkState::Empty || slot.lastTriRevision != m_CurrentRevision)
							continue;
						csv << slot.chunkIndex << ',' << slot.id.coord.x << ',' << slot.id.coord.z << ','
							<< slot.lastTriCount << ',' << slot.lastTriRevision << ',' << (i32)slot.rejectedRevision << '\n';
					}

					std::error_code fsErr;
					const u64  sweepSize = (u64)std::filesystem::file_size("voxel_tri_sweep.csv", fsErr);
					const bool bNewSweep = fsErr || sweepSize == 0u || sweepSize == (u64)-1;

					std::ofstream sweep("voxel_tri_sweep.csv", std::ios::app);
					if (bNewSweep)
						sweep << "label,samples,rejected,p50,p90,p99,max\n";
					sweep << sweepLabel << ',' << samples.size() << ',' << numRejected << ','
						  << pct(0.50f) << ',' << pct(0.90f) << ',' << pct(0.99f) << ',' << samples.back() << '\n';
				}
				ImGui::SameLine();
				ImGui::TextDisabled("-> voxel_tri_stats_<label>.csv + voxel_tri_sweep.csv row");
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Slot table"))
		{
			static const char* kStateNames[] = { "Empty", "Queued", "Resident", "Dirty", "ResidentEmpty" };
			if (ImGui::BeginTable("slots", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("slot");
				ImGui::TableSetupColumn("state");
				ImGui::TableSetupColumn("coord");
				ImGui::TableSetupColumn("originWS");
				ImGui::TableSetupColumn("page");
				ImGui::TableSetupColumn("builtRev");
				ImGui::TableSetupColumn("lastTri");
				ImGui::TableHeadersRow();

				for (const ChunkSlot& slot : m_ChunkSlots)
				{
					if (slot.state == eChunkState::Empty)
						continue;

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::Text("%u", slot.chunkIndex);
					ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(kStateNames[(u32)slot.state]);
					ImGui::TableSetColumnIndex(2); ImGui::Text("(%d, %d) L%u", slot.id.coord.x, slot.id.coord.z, slot.id.lod);
					ImGui::TableSetColumnIndex(3); ImGui::Text("(%.1f, %.1f)", slot.originWS.x, slot.originWS.z);
					ImGui::TableSetColumnIndex(4);
					if (slot.pageID == kInvalidIndex) ImGui::TextUnformatted("-");
					else                              ImGui::Text("%s%u", kClassNames[VoxelPageClassId(slot.pageID)], VoxelPageIdx(slot.pageID));
					ImGui::TableSetColumnIndex(5);
					if (slot.builtRevision == kInvalidIndex) ImGui::TextUnformatted("-");
					else                                     ImGui::Text("%u", slot.builtRevision);
					ImGui::TableSetColumnIndex(6);
					if (slot.lastTriRevision == kInvalidIndex) ImGui::TextUnformatted("-");
					else                                       ImGui::Text("%u", slot.lastTriCount);
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
	}
	ImGui::End();
}


} // namespace baamboo
