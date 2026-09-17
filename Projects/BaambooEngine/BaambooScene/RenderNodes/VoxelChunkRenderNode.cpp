#include "BaambooPch.h"
#include "VoxelChunkRenderNode.h"

#include "ShaderTypes.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"

#include "BaambooScene/Scene.h"
#include "BaambooScene/VoxelTerrain/MarchingCubes.h"
#include "BaambooScene/VoxelTerrain/TransvoxelTables.h"


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

// Face of chunkA touching chunkB: bit 0..5 = -x,+x,-y,+y,-z,+z; kInvalidIndex if not face-adjacent
u32 FaceBitToward(VoxelChunkID chunkA, VoxelChunkID chunkB)
{
	int3 beginA = chunkA.coord * (1 << chunkA.lod);
	int3 endA   = (chunkA.coord + 1) * (1 << chunkA.lod);
	int3 beginB = chunkB.coord * (1 << chunkB.lod);
	int3 endB   = (chunkB.coord + 1) * (1 << chunkB.lod);

	const bool bOverlapX = beginA.x < endB.x && beginB.x < endA.x;
	const bool bOverlapY = beginA.y < endB.y && beginB.y < endA.y;
	const bool bOverlapZ = beginA.z < endB.z && beginB.z < endA.z;

	if (bOverlapY && bOverlapZ && beginA.x == endB.x) return 0u;
	if (bOverlapY && bOverlapZ && endA.x == beginB.x) return 1u;
	if (bOverlapX && bOverlapZ && beginA.y == endB.y) return 2u;
	if (bOverlapX && bOverlapZ && endA.y == beginB.y) return 3u;
	if (bOverlapX && bOverlapY && beginA.z == endB.z) return 4u;
	if (bOverlapX && bOverlapY && endA.z == beginB.z) return 5u;
	return kInvalidIndex;
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
			.count              = kMaxVoxelChunkSlots,
			.elementSizeInBytes = sizeof(VoxelChunkCounts),
			.bufferUsage        = eBufferUsage_Storage,
		});
	m_pChunkDescBuffer = Buffer::Create(rd, "VoxelChunkPass::ChunkDescs",
		{
			.count              = kMaxVoxelChunkSlots,
			.elementSizeInBytes = sizeof(VoxelChunkDesc),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});
	for (VoxelChunkDesc& desc : m_ChunkDescs)
		desc.pageID = kInvalidIndex;
	for (u32 c = 0u; c < kVoxelPageClassCount; ++c)
		m_FreePages[c].reserve(VoxelClassPageCount(c));
	m_FreeErosionSlices.reserve(kMaxVoxelErosionSlices);

	// Density volume the extract samples, linear (C+1+2A)^3
	const u32 kDensityVoxelCount = kDensityVolumeDim * kDensityVolumeDim * kDensityVolumeDim;
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
	m_pTransvoxelTable = Buffer::Create(rd, "VoxelChunkPass::TransvoxelTable",
		{
			.count              = TransvoxelTables::kFlatTableSizeU32,
			.elementSizeInBytes = sizeof(u32),
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

	auto pTransvoxelCS = Shader::Create(rd, "VoxelTransvoxelCS",
		{ .stage = eShaderStage::Compute, .filename = "VoxelTransvoxelCS" });
	m_pTransvoxelPSO = ComputePipeline::Create(rd, "VoxelTransvoxelPSO");
	m_pTransvoxelPSO->SetComputeShader(pTransvoxelCS).Build();

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

	m_FreeSlots.reserve(kMaxVoxelChunkSlots);
	for (u32 i = kMaxVoxelChunkSlots; i > 0u; --i)
		m_FreeSlots.push_back(i - 1u); // back = lowest index, matching the old low-first linear search
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

	assert(m_SlotIdMap.contains(VoxelChunkKey(chunkSlot.id)));
	m_SlotIdMap.erase(VoxelChunkKey(chunkSlot.id));

	chunkSlot = {};
	chunkSlot.chunkIndex = slotIndex;
	m_FreeSlots.push_back(slotIndex);

	m_ChunkDescs[slotIndex] = {};
	m_ChunkDescs[slotIndex].pageID = kInvalidIndex;
}

u32 VoxelChunkRenderNode::AllocatePageOrReclaim(u32 classId, const float3& camPos, float baseChunkSizeMeter)
{
	const u32 pageID = AllocatePage(classId);
	if (pageID != kInvalidIndex)
		return pageID;

	// reclaim the farthest retiring chunk holding a page of this pool; its built members go visible now (partial swap)
	ChunkSlot* victim = nullptr;
	float farthest = -1.0f;
	for (ChunkSlot& slot : m_ChunkSlots)
	{
		if (slot.state == eChunkState::Empty || slot.bDesired || !slot.bVisible)
			continue;

		const bool bReclaimable =
			(slot.pageID != kInvalidIndex && VoxelPageClassId(slot.pageID) == classId) || (slot.pendingPageID != kInvalidIndex && VoxelPageClassId(slot.pendingPageID) == classId);
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
		{
			member.fadeRemaining = 0.0f;
			member.bVisible = true;
		}
	}
	RetireSlot(*victim);

	return AllocatePage(classId);
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
	return std::min(chunkSlot.id.lod, kVoxelPageClassCount - 1u);
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
	if (!m_bStaticTablesUploaded)
	{
		std::vector< i32 > triTable(MarchingCubes::kFlatTriangleTableSize);
		MarchingCubes::FillFlatTriangleTable(triTable.data());
		context.UploadData(m_pMCTriTable, triTable.data(), MarchingCubes::kFlatTriangleTableSize, sizeof(i32), 0);

		std::vector< u32 > tvTable(TransvoxelTables::kFlatTableSizeU32);
		TransvoxelTables::FillFlatTable(tvTable.data());
		context.UploadData(m_pTransvoxelTable, tvTable.data(), TransvoxelTables::kFlatTableSizeU32, sizeof(u32), 0);

		m_bStaticTablesUploaded = true;
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
			return false; // pool of this level exhausted: stays queued until a page frees
	}
	EnsurePageStatics(context, chunkSlot.pageID);

	VoxelChunkDesc desc = {};
	desc.originWS       = float3(int3(gp.chunkCoordX, gp.chunkCoordY, gp.chunkCoordZ) * (i32)gp.cellsPerAxis) * gp.voxelSizeMeter;
	desc.chunkSizeMeter = float(gp.cellsPerAxis) * gp.voxelSizeMeter;
	desc.voxelSizeMeter = gp.voxelSizeMeter;
	desc.lodAndMask     = (chunkSlot.id.lod & 0xFFu) | ((chunkSlot.desiredMask & 0x3Fu) << 8u); // desired-cut mask; G-4 refines to active neighbors
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
	if (!m_pDensityPSO)
		return;

	const u32 dim = gp.samplesPerAxis + 2u * gp.apron; // C+1+2A
	if (dim == 0u || dim > kDensityVolumeDim)
	{
		fprintf(stderr, "[VoxelDensity] dim %u exceeds volume %u -- density skipped.\n", dim, kDensityVolumeDim);
		return;
	}

	BAAMBOO_GPU_SCOPE(context, "Density");

	context.SetRenderPipeline(m_pDensityPSO.get());

	context.TransitionBufferToWrite(m_pDensityField, ePipelineStage::ComputeShader);

	context.SetComputeDynamicUniformBuffer("g_VoxelGenParams", gp);
	context.StageDescriptor("g_OutDensity", m_pDensityField);

	context.Dispatch3D< 4, 4, 4 >(dim, dim, dim);
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

	// Pass A1: marching cubes -- each active cell atomic-appends its per-corner triangle vertices to the page.
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
		float morphRadiusMeter; // geomorph projection search radius
	} mc = { C, apron, gp.voxelSizeMeter, vBase, triCap, 2.0f * gp.voxelSizeMeter }; // projection radius = one parent cell
	context.SetComputeConstants(sizeof(mc), &mc);
	context.StageDescriptor("g_TriTable", m_pMCTriTable);
	context.StageDescriptor("g_DensityField", m_pDensityField);
	context.StageDescriptor("g_MCCounter", m_pMCCounter);
	context.StageDescriptor("g_OutVertices", m_pVertexPool);

	context.Dispatch3D< 4, 4, 4 >(C, C, C);
	context.EndGpuMarker();

	// Pass A2: transvoxel
	context.BeginGpuMarker("TransvoxelExtract");
	context.TransitionBufferToRead(m_pTransvoxelTable, ePipelineStage::ComputeShader);

	context.SetRenderPipeline(m_pTransvoxelPSO.get());
	context.SetComputeConstants(sizeof(mc), &mc);
	context.StageDescriptor("g_TvTables", m_pTransvoxelTable);
	context.StageDescriptor("g_TriTable", m_pMCTriTable);
	context.StageDescriptor("g_DensityField", m_pDensityField);
	context.StageDescriptor("g_MCCounter", m_pMCCounter);
	context.StageDescriptor("g_OutVertices", m_pVertexPool);

	context.Dispatch3D< 8, 8, 1 >(C / 2u, C / 2u, 6u);
	context.EndGpuMarker();

	context.UAVBarrier(m_pMCCounter, true); // extract -> sort
	context.UAVBarrier(m_pVertexPool);

	// Pass A3: triangle spatial sort into Morton blocks, baked into the meshlet-vertex indirection (vertices stay in place).
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

	// A3.1: histogram
	context.SetRenderPipeline(m_pTriSortCountPSO.get());
	context.SetComputeConstants(sizeof(ts), &ts);
	context.StageDescriptor("g_MCCounter", m_pMCCounter);
	context.StageDescriptor("g_Vertices", m_pVertexPool);
	context.StageDescriptor("g_SortBins", m_pTriSortBins);

	context.Dispatch1D< 256 >(triCap);
	context.EndGpuMarker();

	context.UAVBarrier(m_pTriSortBins, true);

	// A3.2: exclusive scan (single group)
	static_assert(kTriSortBins % 1024u == 0u, "scan CS strips assume bins % threads == 0");
	context.BeginGpuMarker("TriSortScan");
	context.SetRenderPipeline(m_pTriSortScanPSO.get());
	struct { u32 numBins; } sc = { kTriSortBins };
	context.SetComputeConstants(sizeof(sc), &sc);
	context.StageDescriptor("g_SortBins", m_pTriSortBins);

	context.Dispatch1D< 1024 >(1024u);
	context.EndGpuMarker();

	context.UAVBarrier(m_pTriSortBins, true);

	// A3.3: scatter the permutation into the meshlet-vertex indirection
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

	for (ChunkSlot& slot : m_ChunkSlots)
		slot.bDesired = false;
	BB_ASSERT(m_SlotIdMap.size() + m_FreeSlots.size() == kMaxVoxelChunkSlots,
		"slot bookkeeping drifted: %u mapped + %u free != %u", (u32)m_SlotIdMap.size(), (u32)m_FreeSlots.size(), kMaxVoxelChunkSlots);

	std::vector< u32 > desiredChunksToAdd;
	for (const VoxelChunkView& view : vt.chunks)
	{
		u64 targetKey = VoxelChunkKey(view.id);
		auto it = m_SlotIdMap.find(targetKey);

		// allocate(queueing) if not resident
		if (it == m_SlotIdMap.end())
		{
			BB_ASSERT(!m_FreeSlots.empty(), "chunk slot pool exhausted: the ring cut exceeds kMaxVoxelChunkSlots");
			ChunkSlot& empty = m_ChunkSlots[m_FreeSlots.back()];
			m_FreeSlots.pop_back();

			empty.id          = view.id;
			empty.originWS    = view.originWS;
			empty.state       = eChunkState::Queued;
			empty.bDesired    = true;
			empty.desiredMask = view.mask;
			if (view.id.lod == 0u)
				empty.erosionSlice = AcquireErosionColumn(view.id);

			desiredChunksToAdd.push_back(empty.chunkIndex);

			m_SlotIdMap.emplace(targetKey, empty.chunkIndex);
		}
		else
		{
			auto& resident = m_ChunkSlots[it->second];

			if (vt.bValid && (resident.state == eChunkState::Resident || resident.state == eChunkState::ResidentEmpty)
				&& resident.builtRevision != vt.revision && resident.pendingRevision != vt.revision && resident.rejectedRevision != vt.revision)
				resident.state = eChunkState::Dirty;

			resident.bDesired    = true;
			resident.desiredMask = view.mask;

			desiredChunksToAdd.push_back(resident.chunkIndex);
		}
	}

	struct SwapCandidate { u32 retireSlot; std::vector< u32 > replacementSlots; };
	std::vector< SwapCandidate > candidates;

	std::vector< u32 > oldSlots;
	std::vector< u32 > oldIndexOf(m_ChunkSlots.size(), kInvalidIndex);
	for (const ChunkSlot& slot : m_ChunkSlots)
	{
		if (slot.state != eChunkState::Empty && slot.bVisible && !slot.bDesired)
		{
			oldIndexOf[slot.chunkIndex] = (u32)oldSlots.size();
			oldSlots.push_back(slot.chunkIndex);
		}
	}

	std::vector< std::vector< u32 > > replacementsOf(oldSlots.size());
	std::vector< bool >               hasVisibleDescendant(m_ChunkSlots.size(), false);

	// visits mapped ancestors coarse-ward until fn returns true
	auto forEachAncestor = [&](VoxelChunkID id, auto&& fn)
	{
		while (id.lod + 1u < kVoxelPageClassCount)
		{
			id = VoxelChunkID{ id.coord >> 1, id.lod + 1u };
			auto it = m_SlotIdMap.find(VoxelChunkKey(id));
			if (it != m_SlotIdMap.end() && fn(m_ChunkSlots[it->second]))
				break;
		}
	};

	for (u32 desiredSlot : desiredChunksToAdd)
	{
		forEachAncestor(m_ChunkSlots[desiredSlot].id, [&](const ChunkSlot& ancestor)
			{
				if (ancestor.bVisible && !ancestor.bDesired)
					replacementsOf[oldIndexOf[ancestor.chunkIndex]].push_back(desiredSlot);
				return false;
			});
	}
	for (u32 oldSlot : oldSlots)
	{
		forEachAncestor(m_ChunkSlots[oldSlot].id, [&](const ChunkSlot& ancestor)
			{
				if (!ancestor.bDesired)
					return false;
				replacementsOf[oldIndexOf[oldSlot]].push_back(ancestor.chunkIndex);
				hasVisibleDescendant[ancestor.chunkIndex] = true;
				return true;
			});
	}

	for (ChunkSlot& slot : m_ChunkSlots)
	{
		if (slot.state == eChunkState::Empty)
			continue;

		// new chunk visible; only if no old still shows in its footprint (an ancestor above it or descendants inside it)
		if (slot.bDesired && !slot.bVisible && IsBuilt(slot))
		{
			bool bOverlaps = hasVisibleDescendant[slot.chunkIndex];
			forEachAncestor(slot.id, [&](const ChunkSlot& ancestor) { bOverlaps |= ancestor.bVisible; return ancestor.bVisible; });

			if (!bOverlaps)
				slot.bVisible = true;
		}

		// old chunk eviction; judged by the 2:1 gate below once all replacements are built
		if (slot.bVisible && !slot.bDesired)
		{
			const std::vector< u32 >& replacementSlots = replacementsOf[oldIndexOf[slot.chunkIndex]];

			bool bAllBuilt = true;
			for (u32 replacementSlot : replacementSlots)
				bAllBuilt = bAllBuilt && IsBuilt(m_ChunkSlots[replacementSlot]);

			// register as candidate to evict current slot and make-visible replacements if all overlapping chunks(=replacements) are built
			if (bAllBuilt && slot.fadeRemaining == 0.0f)
			{
				SwapCandidate& candidate = candidates.emplace_back();
				candidate.retireSlot       = slot.chunkIndex;
				candidate.replacementSlots = replacementSlots;
			}
		}
		else if (!slot.bDesired && !slot.bVisible)
		{
			RetireSlot(slot);
		}
	}

	// 2:1 gate: desired chunks nest 2:1 by construction, so a replacement's only possible 2-step partner is an old that stays visible
	std::vector< bool > alive(candidates.size(), true);

	bool bPruned      = true;
	while (bPruned)
	{
		bPruned = false;
		std::vector< bool > retires(m_ChunkSlots.size(), false);
		for (size_t i = 0; i < candidates.size(); ++i)
		{
			if (alive[i])
				retires[candidates[i].retireSlot] = true;
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

				for (u32 oldSlot : oldSlots)
				{
					const ChunkSlot& s = m_ChunkSlots[oldSlot];
					if (s.state == eChunkState::ResidentEmpty || retires[oldSlot])
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
			}
		}
	}
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		if (!alive[i])
			continue;

		if (vt.crossfadeSeconds <= 0.0f)
		{
			RetireSlot(m_ChunkSlots[candidates[i].retireSlot]);
			for (u32 replacementSlot : candidates[i].replacementSlots)
				m_ChunkSlots[replacementSlot].bVisible = true;
		}
		else
		{
			ChunkSlot& retiring = m_ChunkSlots[candidates[i].retireSlot];
			retiring.fadeRemaining = 1.0f;
			retiring.fadeLastUpdateTime = renderView.time;
			for (u32 replacementSlot : candidates[i].replacementSlots)
			{
				if (!m_ChunkSlots[replacementSlot].bVisible)
				{
					m_ChunkSlots[replacementSlot].fadeRemaining = -1.0f;
					m_ChunkSlots[replacementSlot].fadeLastUpdateTime = renderView.time;
					m_ChunkSlots[replacementSlot].bVisible = true;
				}
			}
		}
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
			// a rejected revision stays rejected (keep-last geometry) -- no bigger pool exists for this level
			if (slot.rejectedRevision == vt.revision)
			{
				slot.state = eChunkState::Resident;
				continue;
			}

			buildPageID = AllocatePageOrReclaim(SelectPageClass(slot), renderView.camera.pos, vt.chunkWorldSizeMeter);
			if (buildPageID == kInvalidIndex)
				continue; // pool of this level exhausted: keeps the last geometry until a page frees
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

	for (auto& slot : m_ChunkSlots)
	{
		if (slot.state == eChunkState::Empty || slot.fadeRemaining == 0.0f)
			continue;

		// New fades start at this snapshot's time, so they consume no earlier interval.
		const float dt = std::max(0.0f, renderView.time - slot.fadeLastUpdateTime);
		slot.fadeLastUpdateTime = renderView.time;
		const float fadeStep = vt.crossfadeSeconds > 0.0f ? dt / vt.crossfadeSeconds : 1.0f;

		if (slot.fadeRemaining > 0.0f)
		{
			slot.fadeRemaining = std::max(0.0f, slot.fadeRemaining - fadeStep);
			if (slot.fadeRemaining == 0.0f)
				RetireSlot(slot);
		}
		else
		{
			slot.fadeRemaining = std::min(0.0f, slot.fadeRemaining + fadeStep);
		}
	}

	// Active-neighbor transition masks: the band state follows what is visible right now, not the desired cut
	{
		std::vector< u32 > visibleSlots;
		visibleSlots.reserve(m_ChunkSlots.size());
		for (const ChunkSlot& slot : m_ChunkSlots)
		{
			if (slot.state != eChunkState::Empty && slot.bVisible)
				visibleSlots.push_back(slot.chunkIndex);
		}

		std::vector< u32 > derivedMasks(m_ChunkSlots.size(), 0u);
		for (size_t i = 0; i < visibleSlots.size(); ++i)
		{
			const auto& fine = m_ChunkSlots[visibleSlots[i]];

			VoxelChunkID coarseFaceIDs[6] =
			{
				{ (fine.id.coord + int3(-1,  0,  0)) >> 1, fine.id.lod + 1 },
				{ (fine.id.coord + int3(+1,  0,  0)) >> 1, fine.id.lod + 1 },
				{ (fine.id.coord + int3( 0, -1,  0)) >> 1, fine.id.lod + 1 },
				{ (fine.id.coord + int3( 0, +1,  0)) >> 1, fine.id.lod + 1 },
				{ (fine.id.coord + int3( 0,  0, -1)) >> 1, fine.id.lod + 1 },
				{ (fine.id.coord + int3( 0,  0, +1)) >> 1, fine.id.lod + 1 }
			};

			for (u32 f = 0; f < 6u; ++f)
			{
				auto it = m_SlotIdMap.find(VoxelChunkKey(coarseFaceIDs[f]));
				if (it == m_SlotIdMap.end())
					continue;

				if (m_ChunkSlots[it->second].state == eChunkState::Empty || m_ChunkSlots[it->second].bVisible == false)
					continue;

				const u32 faceBit = FaceBitToward(fine.id, coarseFaceIDs[f]);
				if (faceBit != kInvalidIndex)
					derivedMasks[fine.chunkIndex] |= 1u << faceBit;
			}
		}

		for (u32 slotIndex : visibleSlots)
			m_ChunkDescs[slotIndex].lodAndMask = (m_ChunkSlots[slotIndex].id.lod & 0xFFu) | (derivedMasks[slotIndex] << 8u);
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
			desc.debugFlags            = vt.debugFlags;
			desc.diceKScale            = kScale;

			desc.microAmplitudeMeter      = vt.dice.microAmplitudeMeter;
			desc.microBaseWaveLengthMeter = vt.dice.microBaseWaveLengthMeter;
			desc.microLacunarity          = vt.dice.microLacunarity;
			desc.microGain                = vt.dice.microGain;
			desc.microCreaseBoost         = vt.dice.microCreaseBoost;
			desc.microSharpness           = vt.dice.microSharpness;
			desc.microOctaves             = vt.dice.microOctaves;

			const bool isFading  = m_ChunkSlots[i].fadeRemaining != 0.0f;
			const u32  threshold = isFading ? 16u - std::min(16u, u32(16.0f * std::abs(m_ChunkSlots[i].fadeRemaining))) : 16u;
			// visible(bit0) | fadeOut(bit1) | ditherThreshold(bits 8..12)
			desc.flags = m_ChunkSlots[i].bVisible ? (1u | ((isFading && m_ChunkSlots[i].fadeRemaining > 0.0f) ? (1u << 1) : 0u) | (threshold << 8)) : 0u;
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
					// over the level's capacity: keep-last geometry stands, retry only on the next revision
					DeallocatePage(slot.pendingPageID);
					slot.rejectedRevision = tag.revision;
				}
				slot.pendingPageID   = kInvalidIndex;
				slot.pendingRevision = kInvalidIndex;
			}
			else if (bLiveVerdict && slot.lastTriCount > tag.triCap)
			{
				slot.rejectedRevision = tag.revision; // clamped initial build stays clamped this revision
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


} // namespace baamboo
