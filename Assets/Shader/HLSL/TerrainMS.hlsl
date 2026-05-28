#define _CAMERA
#include "TerrainCommon.hlsli"
#include "HelperFunctions.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_Heightmap      : register(b0, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_PatchInstances : register(b1, ROOT_CONSTANT_SPACE);

cbuffer CommandSignatureParam : register(b0, COMMMANDSIGNATURE_SPACE)
{
    uint g_DrawID;
};


#define TERRAIN_MAX_VERTS  256
#define TERRAIN_MAX_PRIMS  256
#define TERRAIN_MS_THREADS 128

struct SurfaceVertex
{
    float3 posWS;
    float2 uv;
};

SurfaceVertex ComputeSurfaceVertex(uint gx, uint gz, PatchInstance pi)
{
    Texture2D< float > Heightmap = GetResource(g_Heightmap.index);

    const float patchOriginX = pi.patchOriginX;
    const float patchOriginZ = pi.patchOriginZ;
    const float patchSizeM   = pi.patchSizeMeter;
    const uint  gridN        = pi.gridDim;
    const float rStart       = GetLodRangeStart(pi.depth);
    const float rEnd         = GetLodRangeEnd  (pi.depth);

    float2 gFine   = float2(gx, gz);
    float2 xzWORLD = float2(patchOriginX, patchOriginZ) + gFine * (patchSizeM / (gridN - 1));

    float2 tFine   = (xzWORLD - float2(g_Terrain.TerrainOriginX, g_Terrain.TerrainOriginZ)) / g_Terrain.TerrainSizeMeter;
    float  h01Fine = Heightmap.SampleLevel(g_LinearClampSampler, tFine, 0);

    float distVIEW = distance(g_Camera.posWORLD, float3(xzWORLD.x, g_Terrain.HeightMinMeter + h01Fine * g_Terrain.HeightRangeMeter, xzWORLD.y));
    float alpha    = saturate((distVIEW - rStart) / max((rEnd - rStart), EPSILON_MIN));

    int2   gSnapped = int2(gx, gz) & ~1;
    float2 gMorphed = lerp(gFine, float2(gSnapped), alpha);
    float guMorphed = gMorphed.x / (float)(gridN - 1);
    float gvMorphed = gMorphed.y / (float)(gridN - 1);

    float xWORLD = patchOriginX + guMorphed * patchSizeM;
    float zWORLD = patchOriginZ + gvMorphed * patchSizeM;
    float tu = (xWORLD - g_Terrain.TerrainOriginX) / g_Terrain.TerrainSizeMeter;
    float tv = (zWORLD - g_Terrain.TerrainOriginZ) / g_Terrain.TerrainSizeMeter;

    float h01 = Heightmap.SampleLevel(g_LinearClampSampler, float2(tu, tv), 0);
    float3 posWS = float3(xWORLD, h01 * g_Terrain.HeightRangeMeter + g_Terrain.HeightMinMeter, zWORLD);

    SurfaceVertex sv;
    sv.posWS = posWS;
    sv.uv    = float2(tu, tv);
    return sv;
}

uint2 PerimGridCoord(uint s, uint gridN)
{
    const uint P = gridN - 1u;
    if (s < P)  return uint2(s, 0u);            // bottom edge  (+x)
    s -= P;
    if (s < P)  return uint2(P, s);             // right  edge  (+z)
    s -= P;
    if (s < P)  return uint2(P - s, P);         // top    edge  (-x)
    s -= P;
    return uint2(0u, P - s);                    // left   edge  (-z)
}

float ComputeSkirtDepth(PatchInstance pi)
{
    float gap   = pi.patchSizeMeter / (pi.gridDim - 1);
    float slope = g_Terrain.HeightRangeMeter / g_Terrain.TerrainSizeMeter;
    return 1.0 * slope * gap;
}

[numthreads(TERRAIN_MS_THREADS, 1, 1)]
[outputtopology("triangle")]
void main(
    uint3 GTid : SV_GroupThreadID,
    out vertices MSOutput verts[TERRAIN_MAX_VERTS],
    out indices  uint3    prims[TERRAIN_MAX_PRIMS])
{
    StructuredBuffer< PatchInstance > patches = GetResource(g_PatchInstances.index);
    const PatchInstance pi = patches[g_DrawID];

    const uint gridN = pi.gridDim;
    const uint P     = gridN - 1u;

    const uint surfVerts  = gridN * gridN;
    const uint surfPrims  = 2u * P * P;
    const uint skirtVerts = 4u * P;
    const uint skirtPrims = 8u * P;
    const uint totalVerts = surfVerts + skirtVerts;
    const uint totalPrims = surfPrims + skirtPrims;
    SetMeshOutputCounts(totalVerts, totalPrims);

    const float skirtDepth = ComputeSkirtDepth(pi);

    // --- Vertices: [0,surfVerts) surface grid, then [surfVerts,..) skirt ---
    for (uint v = GTid.x; v < totalVerts; v += TERRAIN_MS_THREADS)
    {
        if (v < surfVerts)
        {
            SurfaceVertex sv = ComputeSurfaceVertex(v % gridN, v / gridN, pi);
            verts[v].positionCS = mul(g_Camera.mViewProj, float4(sv.posWS, 1.0));
            verts[v].uv         = sv.uv;
            verts[v].skirtBlend = 0.0; // surface: pure ground material
            verts[v].patchDepth = pi.depth;      // debug
        }
        else
        {
            const uint s = v - surfVerts;
            uint2 g  = PerimGridCoord(s, gridN);

            SurfaceVertex sv = ComputeSurfaceVertex(g.x, g.y, pi);
            verts[v].positionCS = mul(g_Camera.mViewProj, float4(sv.posWS.x, sv.posWS.y - skirtDepth, sv.posWS.z, 1.0));
            verts[v].uv         = sv.uv;
            verts[v].skirtBlend = 1.0;
            verts[v].patchDepth = pi.depth;      // debug
        }
    }

    // --- Primitives: [0,surfPrims) surface grid, then skirt quads ---
    for (uint p = GTid.x; p < totalPrims; p += TERRAIN_MS_THREADS)
    {
        if (p < surfPrims)
        {
            uint cid = p / 2u;
            uint px  = cid % P;
            uint pz  = cid / P;
            if (p % 2u == 0u)
                prims[p] = uint3(pz * gridN + px, pz * gridN + px + 1u, (pz + 1u) * gridN + px);
            else
                prims[p] = uint3(pz * gridN + px + 1u, (pz + 1u) * gridN + px + 1u, (pz + 1u) * gridN + px);
        }
        else
        {
            // Skirt quad for perimeter segment [seg, seg+1): the two surface edge vertices + their two pushed-down skirt vertices.
            uint sp  = p - surfPrims;
            uint seg = sp / 2u;
            uint s0  = seg;
            uint s1  = (seg + 1u) % skirtVerts;

            uint2 g0 = PerimGridCoord(s0, gridN);
            uint2 g1 = PerimGridCoord(s1, gridN);
            uint  surf0  = g0.y * gridN + g0.x;
            uint  surf1  = g1.y * gridN + g1.x;
            uint  skirt0 = surfVerts + s0;
            uint  skirt1 = surfVerts + s1;

            if (sp % 2u == 0u)
                prims[p] = uint3(surf0, surf1, skirt1);
            else
                prims[p] = uint3(surf0, skirt1, skirt0);
        }
    }
}
