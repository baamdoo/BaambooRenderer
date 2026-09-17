#include "Common.hlsli"
#include "HelperFunctions.hlsli"
#include "VoxelTerrainCommon.hlsli"

#define TV_CLASS_BASE        0u
#define TV_CELLDATA_BASE     512u
#define TV_CELLDATA_STRIDE   10u
#define TV_VERTEXDATA_BASE   1072u
#define TV_VERTEXDATA_STRIDE 6u

cbuffer TvPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_CellsPerAxis;
    uint  g_Apron;
    float g_VoxelSizeMeter;
    uint  g_VertexSlabBase;
    uint  g_MaxTriangles;
    float g_GeomorphRadiusMeter;
};

ConstantBuffer< DescriptorHeapIndex > g_DensityField : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MCCounter    : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutVertices  : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TvTables     : register(b4, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TriTable     : register(b5, ROOT_CONSTANT_SPACE);

static StructuredBuffer<float> Density = GetResource(g_DensityField.index);
static StructuredBuffer<uint>  TvTable  = GetResource(g_TvTables.index);
static StructuredBuffer<int>   TriTable = GetResource(g_TriTable.index);

// Reference: https://transvoxel.org/
// return: bit7 = reversed winding, low 7 bits = equivalence class
uint TvCellClass(uint caseCode)
{
    return TvTable[TV_CLASS_BASE + caseCode];
}

// return: high 4-bits = vertex count, low 4-bits = triangle count
uint TvGeometryCounts(uint classIdx)
{
    return TvTable[TV_CELLDATA_BASE + classIdx * TV_CELLDATA_STRIDE];
}

// return: i-th triangulation index (i in [0, 3*triCount)) into the cell's vertex list
uint TvVertexIndex(uint classIdx, uint i)
{
    uint w = TvTable[TV_CELLDATA_BASE + classIdx * TV_CELLDATA_STRIDE + 1u + i / 4u];
    return (w >> ((i % 4u) * 8u)) & 0xFFu;
}

// return: low byte = the edge's two corner locations, one nibble(4bits) each
uint TvVertexData(uint caseCode, uint i)
{
    uint w = TvTable[TV_VERTEXDATA_BASE + caseCode * TV_VERTEXDATA_STRIDE + i / 2u];
    return (w >> ((i % 2u) * 16u)) & 0xFFFFu;
}

// Isosurface vertex on `edge`: position interpolates the two corner samples
VoxelVertex MakeEdgeVertex(uint2 edge, float cornerVal[13], uint3 cornerPos[13], float3 cornerGrad[13], float chunkSizeMeter)
{
    bool bCoarse = (edge.x >= 9u && edge.y >= 9u);

    float  v0 = cornerVal[edge.x], v1 = cornerVal[edge.y];
    float3 p0 = float3(cornerPos[edge.x]), p1 = float3(cornerPos[edge.y]);
    float3 g0 = cornerGrad[edge.x], g1 = cornerGrad[edge.y];

    float  tt = (abs(v0 - v1) < 1e-6) ? 0.0 : v0 / (v0 - v1); // zero crossing along the edge

    float3 pos = lerp(p0, p1, tt) * g_VoxelSizeMeter;
    float3 g   = lerp(g0, g1, tt);
    float  gSq = dot(g, g);
    float3 n   = (gSq > 1e-12) ? g * rsqrt(gSq) : float3(0.0, 1.0, 0.0);

    // Should use the exact same normal value(decoded) as the rendering point(GBuffer) for consistency
    float3 nq = VoxelQuantizeNormal(n);

    VoxelTerrainGenParams gp = (VoxelTerrainGenParams) 0;
    gp.cellsPerAxis   = g_CellsPerAxis;
    gp.apron          = g_Apron;
    gp.voxelSizeMeter = g_VoxelSizeMeter;
    VoxelProjectResult pr1 = VoxelProjectStage(gp, pos, nq, g_GeomorphRadiusMeter, 2u, Density, TriTable);       // s1 = to the parent mesh
    VoxelProjectResult pr2 = VoxelProjectStage(gp, pos, nq, 2.0 * g_GeomorphRadiusMeter, 4u, Density, TriTable); // s2 = to the grandparent
    VoxelProjectResult pr3 = VoxelProjectStage(gp, pos, nq, 4.0 * g_GeomorphRadiusMeter, 8u, Density, TriTable); // s3 = to the great-grandparent

    float s1 = bCoarse ? 0.0 : pr1.sd;         // coarse-side vertices already sit on the parent mesh
    float s2 = (pr2.code == 0u) ? pr2.sd : s1; // no hit: stay on the previous LOD
    float s3 = (pr3.code == 0u) ? pr3.sd : s2;

    // normal morph targets: the ancestors' shading normals where the ray hit
    float3 nT1 = bCoarse ? nq : pr1.nTarget;
    float3 nT2 = (pr2.code == 0u) ? pr2.nTarget : nT1;

    return VoxelPackVertex(pos, nq, nT1, nT2, s1, s2, s3, bCoarse, chunkSizeMeter, g_VoxelSizeMeter);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint C = g_CellsPerAxis;
    if (DTid.x >= C / 2u || DTid.y >= C / 2u)
        return;

    const uint face = DTid.z; // 0..5 = -x,+x,-y,+y,-z,+z
    const uint dim  = C + 1u + 2u * g_Apron;

    uint3 u, v, b;
    switch (face)
    {
    case 0:
        u = uint3(0, 1, 0);
        v = uint3(0, 0, 1);
        b = uint3(0, 0, 0);
        break;
    case 1:
        u = uint3(0, 0, 1);
        v = uint3(0, 1, 0);
        b = uint3(C, 0, 0);
        break;
    case 2:
        u = uint3(0, 0, 1);
        v = uint3(1, 0, 0);
        b = uint3(0, 0, 0);
        break;
    case 3:
        u = uint3(1, 0, 0);
        v = uint3(0, 0, 1);
        b = uint3(0, C, 0);
        break;
    case 4:
        u = uint3(1, 0, 0);
        v = uint3(0, 1, 0);
        b = uint3(0, 0, 0);
        break;
    case 5:
        u = uint3(0, 1, 0);
        v = uint3(1, 0, 0);
        b = uint3(0, 0, C);
        break;
    default:
        return;
    }
    uint3 coords = b + (2 * DTid.x) * u + (2 * DTid.y) * v;

    uint3 patchPoints[13];
    for (uint j = 0u; j < 3u; ++j)
    {
        for (uint i = 0u; i < 3u; ++i)
        {
            uint3 s = coords + i * u + j * v;
            patchPoints[j * 3u + i] = s;
        }
    }
    patchPoints[9]  = patchPoints[0];
    patchPoints[10] = patchPoints[2];
    patchPoints[11] = patchPoints[6];
    patchPoints[12] = patchPoints[8];


    const uint bitOrder[9] = { 0, 1, 2, 7, 8, 3, 6, 5, 4 };
    float  cornerVal [13];
    float3 cornerGrad[13];

    uint patchBits = 0u;
    for (uint i = 0u; i < 9u; ++i)
    {
        uint3 t = patchPoints[i] + g_Apron;
        float d = Density[FlatTexel(t, dim)];

        cornerVal [i] = d;
        cornerGrad[i] = float3(
            Density[FlatTexel(t + uint3(1u, 0u, 0u), dim)] - Density[FlatTexel(t - uint3(1u, 0u, 0u), dim)],
            Density[FlatTexel(t + uint3(0u, 1u, 0u), dim)] - Density[FlatTexel(t - uint3(0u, 1u, 0u), dim)],
            Density[FlatTexel(t + uint3(0u, 0u, 1u), dim)] - Density[FlatTexel(t - uint3(0u, 0u, 1u), dim)]);

        patchBits |= (d < 0.0) ? (1u << bitOrder[i]) : 0u;
    }
    if (patchBits == 0u || patchBits == 0x1FFu)
        return; // no surface

    cornerVal[9]  = cornerVal[0];
    cornerVal[10] = cornerVal[2];
    cornerVal[11] = cornerVal[6];
    cornerVal[12] = cornerVal[8];

    // coarse-face corners: gradient at the coarse level's spacing (+-2 texels) so the normal equals the coarse neighbour's
    [unroll]
    for (uint k = 0u; k < 4u; ++k)
    {
        uint3 t = patchPoints[9u + k] + g_Apron;
        cornerGrad[9u + k] = float3(
            Density[FlatTexel(t + uint3(2u, 0u, 0u), dim)] - Density[FlatTexel(t - uint3(2u, 0u, 0u), dim)],
            Density[FlatTexel(t + uint3(0u, 2u, 0u), dim)] - Density[FlatTexel(t - uint3(0u, 2u, 0u), dim)],
            Density[FlatTexel(t + uint3(0u, 0u, 2u), dim)] - Density[FlatTexel(t - uint3(0u, 0u, 2u), dim)]);
    }

    uint tvClass   = TvCellClass(patchBits);
    uint winding   = (tvClass >> 7) & 0b1;
    uint classBits = tvClass & 0b01111111;

    uint tvGeoCounts = TvGeometryCounts(classBits);
    uint vertexCount = (tvGeoCounts >> 4) & 0b1111;
    uint triCount    = tvGeoCounts & 0b1111;

    const float chunkSize = float(C) * g_VoxelSizeMeter;

    RWByteAddressBuffer Counter = GetResource(g_MCCounter.index);
    uint baseTri;
    Counter.InterlockedAdd(0u, triCount, baseTri); // reserve a contiguous triangle range

    RWStructuredBuffer< VoxelVertex > OutV = GetResource(g_OutVertices.index);
    for (uint t = 0u; t < triCount; ++t)
    {
        if (baseTri + t >= g_MaxTriangles)
            break;

        uint i0 = TvVertexIndex(classBits, t * 3u + 0);
        uint i1 = TvVertexIndex(classBits, t * 3u + 1);
        uint i2 = TvVertexIndex(classBits, t * 3u + 2);

        uint p0 = TvVertexData(patchBits, i0);
        uint p1 = TvVertexData(patchBits, i1);
        uint p2 = TvVertexData(patchBits, i2);

        uint2 e0 = uint2((p0 >> 4u) & 0xF, (p0 >> 0u) & 0xF);
        uint2 e1 = uint2((p1 >> 4u) & 0xF, (p1 >> 0u) & 0xF);
        uint2 e2 = uint2((p2 >> 4u) & 0xF, (p2 >> 0u) & 0xF);

        uint v0 = g_VertexSlabBase + (baseTri + t) * 3u + 0u;
        uint v1 = g_VertexSlabBase + (baseTri + t) * 3u + 1u;
        uint v2 = g_VertexSlabBase + (baseTri + t) * 3u + 2u;
        if (winding == 0u)
        {
            OutV[v0] = MakeEdgeVertex(e0, cornerVal, patchPoints, cornerGrad, chunkSize);
            OutV[v1] = MakeEdgeVertex(e2, cornerVal, patchPoints, cornerGrad, chunkSize);
            OutV[v2] = MakeEdgeVertex(e1, cornerVal, patchPoints, cornerGrad, chunkSize);
        }
        else
        {
            OutV[v0] = MakeEdgeVertex(e0, cornerVal, patchPoints, cornerGrad, chunkSize);
            OutV[v2] = MakeEdgeVertex(e2, cornerVal, patchPoints, cornerGrad, chunkSize);
            OutV[v1] = MakeEdgeVertex(e1, cornerVal, patchPoints, cornerGrad, chunkSize);
        }
    }
}
