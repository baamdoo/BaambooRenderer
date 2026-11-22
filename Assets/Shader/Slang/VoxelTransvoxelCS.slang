#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/VoxelMeshing.slangh"

struct VoxelTransvoxelPush
{
    VoxelExtractConstants constants;

    uint ID_TvTables;
    uint ID_TriTable;
    uint ID_DensityField;
    uint ID_MCCounter;
    uint ID_OutVertices;
    uint ID_Triplets;
};

[[vk::push_constant]] ConstantBuffer< VoxelTransvoxelPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Reference: https://transvoxel.org/
// return: bit7 = reversed winding, low 7 bits = equivalence class
uint TvCellClass(StructuredBuffer< uint > tvTable, uint caseCode)
{
    return tvTable[kTransvoxelClassBase + caseCode];
}

// return: high 4-bits = vertex count, low 4-bits = triangle count
uint TvGeometryCounts(StructuredBuffer< uint > tvTable, uint classIdx)
{
    return tvTable[kTransvoxelCellDataBase + classIdx * kTransvoxelCellDataStride];
}

// return: i-th triangulation index (i in [0, 3*triCount)) into the cell's vertex list
uint TvVertexIndex(StructuredBuffer< uint > tvTable, uint classIdx, uint i)
{
    uint w = tvTable[kTransvoxelCellDataBase + classIdx * kTransvoxelCellDataStride + 1u + i / 4u];
    return (w >> ((i % 4u) * 8u)) & 0xFFu;
}

// return: low byte = the edge's two corner locations, one nibble(4bits) each
uint TvVertexData(StructuredBuffer< uint > tvTable, uint caseCode, uint i)
{
    uint w = tvTable[kTransvoxelVertexDataBase + caseCode * kTransvoxelVertexDataStride + i / 2u];
    return (w >> ((i % 2u) * 16u)) & 0xFFFFu;
}

// Each transition cell appends its triangles with 3 private vertices each
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const VoxelExtractConstants ex = g_Push.constants;

    const uint C = ex.cellsPerAxis;
    if (DTid.x >= C / 2u || DTid.y >= C / 2u)
        return;

    const uint face = DTid.z; // 0..5 = -x,+x,-y,+y,-z,+z
    const uint dim  = C + 1u + 2u * ex.apron;

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
    uint3 coords = b + (2u * DTid.x) * u + (2u * DTid.y) * v;

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

    StructuredBuffer< float > density = GetResource(g_Push.ID_DensityField);

    const uint bitOrder[9] = { 0, 1, 2, 7, 8, 3, 6, 5, 4 };
    float  cornerVal [13];
    float3 cornerGrad[13];

    uint patchBits = 0u;
    for (uint i = 0u; i < 9u; ++i)
    {
        uint3 t = patchPoints[i] + ex.apron;
        float d = density[FlatTexel(t, dim)];

        cornerVal [i] = d;
        cornerGrad[i] = VoxelDensityGradient(density, t, 1u, dim);

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
        uint3 t = patchPoints[9u + k] + ex.apron;
        cornerGrad[9u + k] = VoxelDensityGradient(density, t, 2u, dim);
    }

    StructuredBuffer< uint > tvTable = GetResource(g_Push.ID_TvTables);

    uint tvClass   = TvCellClass(tvTable, patchBits);
    uint winding   = (tvClass >> 7) & 0x1u;
    uint classBits = tvClass & 0x7Fu;

    uint triCount  = TvGeometryCounts(tvTable, classBits) & 0xFu;

    RWStructuredBuffer< uint > counter = GetResource(g_Push.ID_MCCounter);
    uint vb;
    InterlockedAdd(counter[kVoxelCounterVertex], 3u * triCount, vb); // reserve 3 private vertices per tri
    uint fitTris = min(triCount, (ex.maxVertices - min(vb, ex.maxVertices)) / 3u); // tris whose vertices fit under the cap
    uint baseTri;
    InterlockedAdd(counter[kVoxelCounterTri], fitTris, baseTri); // reserve a contiguous triangle range

    StructuredBuffer< int >           triTable    = GetResource(g_Push.ID_TriTable);
    RWStructuredBuffer< VoxelVertex > outVertices = GetResource(g_Push.ID_OutVertices);
    RWStructuredBuffer< uint >        triplets    = GetResource(g_Push.ID_Triplets);
    for (uint t = 0u; t < fitTris; ++t)
    {
        if (baseTri + t >= ex.maxTriangles)
            break;

        uint i0 = TvVertexIndex(tvTable, classBits, t * 3u + 0u);
        uint i1 = TvVertexIndex(tvTable, classBits, t * 3u + 1u);
        uint i2 = TvVertexIndex(tvTable, classBits, t * 3u + 2u);

        uint p0 = TvVertexData(tvTable, patchBits, i0);
        uint p1 = TvVertexData(tvTable, patchBits, i1);
        uint p2 = TvVertexData(tvTable, patchBits, i2);

        uint2 e0 = uint2((p0 >> 4u) & 0xFu, (p0 >> 0u) & 0xFu);
        uint2 e1 = uint2((p1 >> 4u) & 0xFu, (p1 >> 0u) & 0xFu);
        uint2 e2 = uint2((p2 >> 4u) & 0xFu, (p2 >> 0u) & 0xFu);

        uint2 edges[3] = { e0, (winding == 0u) ? e2 : e1, (winding == 0u) ? e1 : e2 }; // winding order: e0, e2, e1; reversed-winding classes keep table order

        uint v0 = vb + t * 3u; // page-relative
        [unroll] for (uint k = 0u; k < 3u; ++k)
        {
            uint2 e = edges[k]; // table endpoints are already in min -> max order
            outVertices[ex.vertexPageBase + v0 + k] = VoxelMakeEdgeVertex(ex, cornerVal[e.x], cornerVal[e.y], float3(patchPoints[e.x]) * ex.voxelSizeMeter, float3(patchPoints[e.y]) * ex.voxelSizeMeter,
                                                                          cornerGrad[e.x], cornerGrad[e.y], e.x >= 9u && e.y >= 9u, density, triTable);
            triplets[(baseTri + t) * 3u + k] = v0 + k; // identity triplet
        }
    }
}
