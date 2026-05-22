#ifndef _TERRAIN_COMMON_HEADER
#define _TERRAIN_COMMON_HEADER
#include "Common.hlsli"


#define TERRAIN_MAX_LOD_DEPTHS 8
#define TERRAIN_LOD_FLOAT4S    (TERRAIN_MAX_LOD_DEPTHS / 4) // 2 × float4 packs 8 floats

struct TerrainParams
{
    float TerrainOriginX;
    float TerrainOriginZ;
    float TerrainSizeMeter; // T
    uint  GridN;            // N (per-patch vertex count per edge)

    float HeightMinMeter;   // minY
    float HeightRangeMeter; // R
    float HeightmapTexel;   // 1 / rho
    float WorldPerTexel;    // T / rho

    uint  HeightmapRez;     // rho
    uint  MaxDepth;         // 0..maxDepth (root..leaf)
    uint  NumPatches;       // emit count this frame (debug)
    uint  _pad0;

    float4 LodRangeStartPacked[TERRAIN_LOD_FLOAT4S];
    float4 LodRangeEndPacked  [TERRAIN_LOD_FLOAT4S];
};
ConstantBuffer<TerrainParams> g_Terrain : register(b0, space1);

float GetLodRangeStart(uint depth) { return g_Terrain.LodRangeStartPacked[depth >> 2][depth & 3]; }
float GetLodRangeEnd  (uint depth) { return g_Terrain.LodRangeEndPacked  [depth >> 2][depth & 3]; }

ConstantBuffer< DescriptorHeapIndex > g_Heightmap      : register(b0, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_PatchInstances : register(b1, ROOT_CONSTANT_SPACE);

struct PatchInstance
{
    float patchOriginX;
    float patchOriginZ;
    float patchSizeM;
    uint  depth;
    uint  gridDim;
};

struct MSOutput
{
    float4 positionCS : SV_Position;
    float3 normalWS   : TEXCOORD0;
    float2 uv         : TEXCOORD1; // patch-local (u,v) — debug overlay 용
};

#endif // _TERRAIN_COMMON_HEADER
