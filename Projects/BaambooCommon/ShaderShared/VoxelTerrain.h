#pragma once
#include "Types.h"

static const u32 kVoxelChunkInstanceBase = 0u;
static const u32 kMaxVoxelChunkSlots     = 1024u; // Reserved voxel instance slots (= max concurrent resident chunks)
static const u32 kVoxelErosionApron      = 2u;    // erosion map apron texels per side (interior = dim - 2 * apron)

static const float kVoxelWorldFloorYMeter    = 0.0f; // absolute terrain floor plane (below it is always air)
static const u32   kVoxelTrianglesPerMeshlet = 21u;  // 21 tris * 3 = 63 verts fits the 64-vertex mesh-shader limit

// Triangle spatial sort: Morton-coded blocks over the chunk cube, scanned by a single group
static const u32 kVoxelTriSortBlocksPerAxis = 32u;
static const u32 kVoxelTriSortBins          = kVoxelTriSortBlocksPerAxis * kVoxelTriSortBlocksPerAxis * kVoxelTriSortBlocksPerAxis;
static const u32 kVoxelTriSortScanThreads   = 1024u;

// MC counter fields, one u32 each
static const u32 kVoxelCounterTri           = 0u; // triangles appended by marching cubes and transvoxel
static const u32 kVoxelCounterActiveCell    = 1u; // cells the surface crosses
static const u32 kVoxelCounterVertex        = 2u; // vertex slots reserved by edge mark and transvoxel
static const u32 kVoxelCounterMeshletVertex = 3u; // meshlet-vertex slots reserved by the meshlet build
static const u32 kVoxelCounterEdgeVertex    = 4u; // edge vertices the vertex pass writes: min(vertex, vertex cap) right after edge mark
static const u32 kVoxelCounterFields        = 5u;

// Transvoxel flat table offsets (u32 units); TransvoxelTables documents the layout
static const u32 kTransvoxelClassBase        = 0u;
static const u32 kTransvoxelCellDataBase     = 512u;
static const u32 kTransvoxelCellDataStride   = 10u;
static const u32 kTransvoxelVertexDataBase   = kTransvoxelCellDataBase + 56u * kTransvoxelCellDataStride;
static const u32 kTransvoxelVertexDataStride = 6u;

#ifdef __cplusplus
struct VoxelChunkID
{
    int3 coord = int3(0);
    u32  lod   = 0u;
};
constexpr u64 VoxelChunkKey(int3 coords, u32 lod) { return (static_cast<u64>(lod & 0xFFFF) << 48u) | (static_cast<u64>(coords.x & 0xFFFF) << 32u) | (static_cast<u64>(coords.y & 0xFFFF) << 16u) | static_cast<u64>(coords.z & 0xFFFF); }
constexpr u64 VoxelChunkKey(VoxelChunkID id) { return VoxelChunkKey(id.coord, id.lod); }
#endif

// =========================================================================
// Voxel Chunk
// =========================================================================
struct VoxelChunkDesc
{
    float3 originWS;
    u32    vOffset;

    u32   mvOffset;
    u32   mtOffset;
    float chunkSizeMeter;
    u32   diceMaxLevel;          // micro-dicing max subdivision level (0 = off, 1..3)

    // Micro-dicing
    float diceRadiusMeter;
    float diceFadeWidthMeter;    // displacement fades to 0 approaching the radius
    float diceDisplacementScale;
    u32   debugFlags;            // bit0 = chunk tint | bit1 = LOD tint

    // Adaptive dicing
    float diceTargetPx;        // target sub-edge screen size (px)
    float diceKScale;          // K = 0.5 * viewportH * |P11|
    float voxelSizeMeter;
    float microAmplitudeMeter;

    float microBaseWaveLengthMeter; // largest micro octave wavelength
    float microLacunarity;
    float microGain;
    float microCreaseBoost;         // erosion crease/ridge amplitude modulation strength

    u32   microOctaves;
    float microSharpness; // -1 = ridged .. 0 = plain .. +1 = billowed
    u32   pageID;
    u32   lodAndMask;     // lod u8 | transitionMask u8

    u32 mOffset;          // absolute meshlet-pool base of this chunk's page
    u32 erosionSlice;     // erosion detail array slice (independent of pageID)
    u32 flags;            // bit0 = renderable | bit1 = fadeOut | bits 8..12 = dither threshold
    u32 reserved;
};

struct VoxelChunkCounts
{
    u32 meshletCount;
};

struct VoxelMeshDataPatchConstants
{
    u32 voxelMeshBaseID;
    u32 numVoxelSlots;
    u32 padding0;
    u32 padding1;
};

// 16B voxel vertex: quantized chunk-local position, three 8+8 octahedral normals, three 10-bit geomorph offsets
struct VoxelVertex
{
    u32 posXY;      // pos.x 16b | pos.y 16b
    u32 posZnormal; // pos.z 16b | vertex normal 8b+8b
    u32 normalT;    // geomorph target normals: parent 8b+8b (bits 16..31) | grandparent 8b+8b (bits 0..15)
    u32 morph;      // bit0 pinned | s1 10b (bits 1..10) | s2 10b (bits 11..20) | s3 10b (bits 21..30): signed offsets along the normal to the parent / grandparent / great-grandparent mesh (units 2/4/8 voxels)
};

// =========================================================================
// Voxel Terrain Generation Params
// =========================================================================
struct VoxelTerrainGenParams
{
    i32   chunkCoordX;
    i32   chunkCoordY;
    i32   chunkCoordZ;
    float voxelSizeMeter;

    u32 cellsPerAxis;
    u32 samplesPerAxis;
    u32 apron;
    u32 seed;

    float frequency;
    u32   octaves;
    float lacunarity;
    float gain;

    float warpStrength;
    float warpFrequency;
    float mountainAmplitude;
    float detailWeight;

    float redistributionExp;
    float ridgedBlend;
    float surfaceBaseYMeter;
    float erosionScale;

    float erosionStrength;
    float erosionGullyWeight;
    float erosionDetail;
    float erosionOnsetInput;
          
    float erosionOnsetOctave;
    float erosionCellScale;
    float erosionNormalization;
    float erosionSlopeScale;

    u32   erosionOctaves;
    float geoMinWavelengthMeter;
    u32   padding1;
    u32   padding2;
};

struct VoxelErosionBakeConstants
{
    VoxelTerrainGenParams genParams;

    u32 erosionSlice; // erosion map array slice of the chunk being baked
    u32 padding0;
    u32 padding1;
    u32 padding2;
};

// =========================================================================
// Voxel Meshing Constants
// =========================================================================
// Edge mark, edge args, edge vertex, marching cubes and transvoxel passes
struct VoxelExtractConstants
{
    u32   cellsPerAxis;   // C; volume dim = C + 1 + 2A
    u32   apron;          // A apron texels per side
    float voxelSizeMeter;
    u32   vertexPageBase; // first vertex of this page in the vertex pool

    u32   maxTriangles;
    float morphRadiusMeter; // geomorph projection search radius
    u32   maxVertices;
    u32   padding0;
};

struct VoxelTriSortConstants
{
    u32 vertexPageBase;
    u32 maxTriangles;
    u32 padding0;
    u32 padding1;
};

struct VoxelMeshletBuildConstants
{
    u32 chunkID;
    u32 meshletPageBase;
    u32 maxMeshlets;
    u32 maxTriangles;

    u32 vertexPageBase;
    u32 meshletVertexPageBase;
    u32 meshletTriPageBase;
    u32 maxVertices;

    u32   maxMeshletVertices;
    float chunkSizeMeter;
    u32   padding0;
    u32   padding1;
};

#ifdef __cplusplus
static_assert(sizeof(VoxelChunkDesc) == 112);
static_assert(sizeof(VoxelChunkCounts) == 4);
static_assert(sizeof(VoxelMeshDataPatchConstants) == 16);
static_assert(sizeof(VoxelVertex) == 16);
static_assert(sizeof(VoxelTerrainGenParams) == 128);
static_assert(sizeof(VoxelErosionBakeConstants) == 144);
static_assert(sizeof(VoxelExtractConstants) == 32);
static_assert(sizeof(VoxelTriSortConstants) == 16);
static_assert(sizeof(VoxelMeshletBuildConstants) == 48);
static_assert(kVoxelTriSortBins % kVoxelTriSortScanThreads == 0u, "the scan strips assume bins % threads == 0");
#endif
