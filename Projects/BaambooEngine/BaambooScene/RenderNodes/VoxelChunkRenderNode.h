#pragma once
#include <array>
#include <unordered_map>

#include "RenderCommon/RenderNode.h"
#include "BaambooScene/VoxelTerrain/VoxelTerrainTypes.h"

struct VoxelTerrainRenderView;

namespace baamboo
{


class VoxelChunkRenderNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	static constexpr u32 kDensityVolumeDim = kDefaultVoxelSamplesPerAxis + 2u * kVoxelDensityApron;

	// Edge map key = ((z * (C+1) + y) * (C+1) + x) * 3 + axis over the (C+1)^3 grid points
	static constexpr u32 kEdgeMapSlots    = 3u * kDefaultVoxelSamplesPerAxis * kDefaultVoxelSamplesPerAxis * kDefaultVoxelSamplesPerAxis;
	static constexpr u32 kEdgeListSlots   = VoxelMaxClassVertexCap();
	static constexpr u32 kTriTripletSlots = 3u * VoxelMaxClassTriCap();

	// Morton-coded 32^3 sort blocks over the chunk cube (a multiple of the 1024-thread scan pass).
	static constexpr u32 kTriSortBins = 32u * 32u * 32u;

	// Erosion detail map (XZ heightfield): interior texels + apron overlap per side for seam-free boundary filtering.
	static constexpr u32 kErosionApron       = 2u;
	static constexpr u32 kErosionMapInnerDim = 512u;
	static constexpr u32 kErosionMapDim      = kErosionMapInnerDim + 2u * kErosionApron;

	enum class eChunkState
	{
		Empty,         // no chunk mapped to this slot
		Queued,        // mapped, build pending
		Resident,      // built and renderable
		Dirty,         // resident but stale
		ResidentEmpty, // built but the surface does not cross it: page returned, slot retained
	};
	// Fixed-slot residency entry: index = chunkIndex
	struct ChunkSlot
	{
		eChunkState state = eChunkState::Empty;

		VoxelChunkID id;
		float3 originWS         = float3(0.0f);
		u32    pageID           = kInvalidIndex; // classId(8b) << 24 | pageIdx(24b)
		u32    erosionSlice     = kInvalidIndex;
		u32    chunkIndex       = kInvalidIndex;
		u32    builtRevision    = kInvalidIndex;
		u32    pendingPageID    = kInvalidIndex; // build-before-swap page awaiting its readback verdict
		u32    pendingRevision  = kInvalidIndex;
		u32    rejectedRevision = kInvalidIndex; // last revision whose build overflowed
		float  fadeRemaining    = 0.0f;          // signed fraction: +1 -> 0 fades out, -1 -> 0 fades in; 0 is inactive
		float  fadeLastUpdateTime = 0.0f;        // scene time in seconds; initialized when the fade starts

		bool bDesired = false; // member of this frame's cut
		bool bVisible = false;
	};

	VoxelChunkRenderNode(render::RenderDevice& rd);
	virtual ~VoxelChunkRenderNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;

	// GPU geometry build (density -> marching cubes -> vertex/meshlet pools + chunk counts); driven by CullingNode.
	void BuildChunkGeometryIfNeeded(render::CommandContext& context, const SceneRenderView& renderView);

private:
	bool EnsureChunkResident(render::CommandContext& context, const VoxelTerrainGenParams& gp, ChunkSlot& chunkSlot, const float3& camPos, float baseChunkSizeMeter, bool bFadingOnly);

	void RetireSlot(ChunkSlot& chunkSlot);
	u32  AllocatePageOrReclaim(u32 classId, const float3& camPos, float baseChunkSizeMeter, bool bFadingOnly);

	u32  AllocatePage(u32 classId);
	void DeallocatePage(u32 pageID);
	u32  AllocateErosionSlice();
	void FreeErosionSlice(u32 slice);
	u32  AcquireErosionColumn(const VoxelChunkID& id);
	void ReleaseErosionColumn(const VoxelChunkID& id);
	bool ConsumeErosionBake(const VoxelChunkID& id, u32 revision);

	u32  SelectPageClass(const ChunkSlot& chunkSlot) const;
	void InstallPage(ChunkSlot& chunkSlot, u32 pageID);
	void EnsureStaticTables(render::CommandContext& context);

	void DispatchDensity(render::CommandContext& context, const VoxelTerrainGenParams& gp, const ChunkSlot& chunkSlot);
	void DispatchExtraction(render::CommandContext& context, const VoxelTerrainGenParams& gp, const ChunkSlot& chunkSlot, u32 buildPageID);
	void DispatchErosionBake(render::CommandContext& context, const VoxelTerrainGenParams& gp, const ChunkSlot& chunkSlot);

	// Reads the oldest ring slot and attributes tri counts to their chunks
	void PublishTriReadback();

private:
	// Persistent geometry pools (device-local)
	Arc< render::Buffer > m_pVertexPool;
	Arc< render::Buffer > m_pMeshletPool;
	Arc< render::Buffer > m_pMeshletVertexPool;
	Arc< render::Buffer > m_pMeshletTrianglePool;

	Arc< render::Buffer > m_pChunkCountsBuffer;
	Arc< render::Buffer > m_pChunkDescBuffer;

	std::array< ChunkSlot, kMaxVoxelChunkSlots >      m_ChunkSlots = {};
	std::array< VoxelChunkDesc, kMaxVoxelChunkSlots > m_ChunkDescs = {};

	std::array< std::vector< u32 >, kVoxelPageClassCount > m_FreePages;         // page indices per class
	std::array< u32, kVoxelPageClassCount >                m_NumAllocatedPages = {};

	std::unordered_map< u64, u32 > m_SlotIdMap;
	std::vector< u32 >             m_FreeSlots;

	std::vector< u32 > m_FreeErosionSlices;
	u32                m_NumAllocatedSlices = 0u;

	// LOD0 XZ column -> shared erosion slice along Y-axis
	struct ErosionColumn
	{
		u32 slice         = kInvalidIndex;
		u32 refCount      = 0u;
		u32 bakedRevision = kInvalidIndex;
	};
	std::unordered_map< u64, ErosionColumn > m_ErosionColumns;

	Box< render::ComputePipeline > m_pMCExtractPSO;
	Box< render::ComputePipeline > m_pTransvoxelPSO;
	Box< render::ComputePipeline > m_pMeshletBuildPSO;
	Box< render::ComputePipeline > m_pEdgeMarkPSO;
	Box< render::ComputePipeline > m_pEdgeArgsPSO;
	Box< render::ComputePipeline > m_pEdgeVertexPSO;

	Arc< render::Buffer >          m_pDensityField; // density volume, linear (C+1+2A)^3
	Box< render::ComputePipeline > m_pDensityPSO;

	Arc< render::Buffer >          m_pHeightTile; // unclamped surface height (m) per (x, z) column, row-major [z][x], (C+1+2A)^2
	Box< render::ComputePipeline > m_pHeightRawPSO;

	// Erosion detail map: RGBA16F = R detail height (m), G ridgeMap, B surfaceY, A unused
	Arc< render::Texture >         m_pErosionDetailMap;
	Box< render::ComputePipeline > m_pErosionBakePSO;

	Arc< render::Buffer > m_pMCTriTable;      // 256x16 MC triangle-edge table (SSBO, uploaded once)
	Arc< render::Buffer > m_pTransvoxelTable; // Transvoxel transition-cell tables, flat u32 (SSBO, uploaded once)
	Arc< render::Buffer > m_pMCCounter;       // [tri, activeCells, vertex, meshletVertex, E]

	// Edge welding scratch, reused by every build
	Arc< render::Buffer > m_pEdgeMap;           // edge key -> page-relative vertex number, kInvalidIndex = no vertex
	Arc< render::Buffer > m_pEdgeList;          // vertex number -> key of its active edge
	Arc< render::Buffer > m_pTriTriplets;       // 3 page-relative vertex numbers per tri, extraction order
	Arc< render::Buffer > m_pTriTripletsSorted; // the same triplets in Morton order
	Arc< render::Buffer > m_pEdgeArgs;          // vertex pass dispatch args (ceil(E/64), 1, 1)

	// Tri-count readback ring: per-build MCCounter snapshot, tagged for attribution when it arrives
	static constexpr u32 kTriReadbackSlots    = kMaxFramesInFlight;
	static constexpr u32 kTriReadbackPerFrame = 4u; // build-budget headroom per frame slot
	static constexpr u32 kMCCounterFields     = 5u;
	struct TriReadbackTag
	{
		u32 chunkIndex = kInvalidIndex;
		u64 chunkKey   = 0u; // chunk identity; the slot may be reused by another chunk before the readback arrives
		u32 revision   = kInvalidIndex;
		u32 triCap     = 0u; // capacities of the page this build ran with
		u32 vertexCap  = 0u;
		u32 mvCap      = 0u;
	};
	Arc< render::Buffer > m_pTriCountReadback;
	std::array< TriReadbackTag, kTriReadbackSlots * kTriReadbackPerFrame > m_TriReadbackTags = {};
	u32 m_TriReadbackIdx          = 0u;
	u32 m_TriReadbackFrameCounter = 0u;

	// Triangle spatial sort: extraction order -> Morton-block order of the tri triplets
	Arc< render::Buffer >          m_pTriSortBins; // kTriSortBins histogram / scanned offsets / scatter cursors
	Box< render::ComputePipeline > m_pTriSortCountPSO;
	Box< render::ComputePipeline > m_pTriSortScanPSO;
	Box< render::ComputePipeline > m_pTriSortScatterPSO;

	bool m_bStaticTablesUploaded = false;

};


} // namespace baamboo
