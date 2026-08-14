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
	static constexpr u32 kMaxChunks        = 1024u;
	static constexpr u32 kMaxResidentPages = VoxelTotalPages();

	static constexpr u32 kMaxTrianglesPerChunk = VoxelClassTriCap(kVoxelPageClassCount - 1u);

	static constexpr u32 kDensityApron     = kVoxelDensityApron;
	static constexpr u32 kDensityVolumeDim = kDefaultVoxelSamplesPerAxis + 2u * kVoxelDensityApron;

	static constexpr u32 kTrianglesPerMeshlet = 21u; // 21 tris * 3 = 63 verts fits the 64-vertex mesh-shader limit

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
		u32    rejectedClassId  = kInvalidIndex; // class that overflowed at rejectedRevision
		u32    lastTriCount     = 0u;            // triangle demand of the last read-back build
		u32    lastTriRevision  = kInvalidIndex; // revision lastTriCount was measured at

		bool bDesired = false; // member of this frame's cut
		bool bVisible = false;         
	};

	VoxelChunkRenderNode(render::RenderDevice& rd);
	virtual ~VoxelChunkRenderNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;
	virtual void DrawUI() override;

	// GPU geometry build (density -> marching cubes -> vertex/meshlet pools + chunk counts); driven by CullingNode.
	void BuildChunkGeometryIfNeeded(render::CommandContext& context, const SceneRenderView& renderView);

private:
	bool EnsureChunkResident(render::CommandContext& context, const VoxelTerrainGenParams& gp, ChunkSlot& chunkSlot, const float3& camPos, float baseChunkSizeMeter);

	void RetireSlot(ChunkSlot& chunkSlot);
	u32  AllocatePageOrReclaim(u32 classId, const float3& camPos, float baseChunkSizeMeter);

	u32  AllocatePage(u32 classId);
	u32  AllocatePageAtLeast(u32 classId); // preferred class first, then larger classes (a larger page always fits)
	void DeallocatePage(u32 pageID);
	u32  AllocateErosionSlice();
	void FreeErosionSlice(u32 slice);
	u32  AcquireErosionColumn(const VoxelChunkID& id);
	void ReleaseErosionColumn(const VoxelChunkID& id);
	bool ConsumeErosionBake(const VoxelChunkID& id, u32 revision);

	u32  SelectPageClass(const ChunkSlot& chunkSlot) const;
	void InstallPage(ChunkSlot& chunkSlot, u32 pageID);
	void EnsurePageStatics(render::CommandContext& context, u32 pageID);

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
	Box< render::ComputePipeline > m_pMeshletBuildPSO;

	Arc< render::Texture >          m_pDensityVolume; // written by VoxelDensityCS; MC reads the linear copy below
	Arc< render::Buffer >           m_pDensityField;  // linear density copy the MC extract samples
	Box< render::ComputePipeline >  m_pDensityPSO;

	// Erosion detail map: RGBA16F = R detail height (m), G ridgeMap, B surfaceY, A unused
	Arc< render::Texture >         m_pErosionDetailMap;
	Box< render::ComputePipeline > m_pErosionBakePSO;

	Arc< render::Buffer > m_pMCTriTable; // 256x16 MC triangle-edge table (SSBO, uploaded once)
	Arc< render::Buffer > m_pMCCounter;  // [triangleCount, activeCellCount]

	// Tri-count readback ring: per-build MCCounter snapshot, tagged for attribution when it arrives
	static constexpr u32 kTriReadbackSlots    = kMaxFramesInFlight;
	static constexpr u32 kTriReadbackPerFrame = 4u; // build-budget headroom per frame slot
	static constexpr u32 kMCCounterFields     = 2u; // [triangleCount, activeCellCount]
	struct TriReadbackTag
	{
		u32 chunkIndex = kInvalidIndex;
		u32 revision   = kInvalidIndex;
		u32 triCap     = 0u; // capacity of the page this build ran with
	};
	Arc< render::Buffer > m_pTriCountReadback;
	std::array< TriReadbackTag, kTriReadbackSlots * kTriReadbackPerFrame > m_TriReadbackTags = {};
	u32 m_TriReadbackIdx          = 0u;
	u32 m_TriReadbackFrameCounter = 0u;

	u32 m_LastBuildChunkIndex = kInvalidIndex;
	u32 m_LastBuildTriCount   = 0u;
	u32 m_LastBuildCellCount  = 0u;
	u32 m_LastBuildTriCap     = 0u;
	u32 m_AllocFailCount      = 0u;
	u32 m_ReclaimCount        = 0u;
	u32 m_NumHeldSwaps        = 0u;
	u32 m_RecenterCount       = 0u;
	u32 m_CurrentRevision     = 0u;

	// Triangle spatial sort: MC append order -> Morton-block order, baked into the meshlet-vertex indirection
	Arc< render::Buffer >          m_pTriSortBins; // kTriSortBins histogram / scanned offsets / scatter cursors
	Box< render::ComputePipeline > m_pTriSortCountPSO;
	Box< render::ComputePipeline > m_pTriSortScanPSO;
	Box< render::ComputePipeline > m_pTriSortScatterPSO;

	bool m_bTriTableUploaded = false;
	// Per-corner identity/pattern index arrays are page-local
	std::array< bool, kMaxResidentPages > m_PageStaticUploaded = {};
};


} // namespace baamboo
