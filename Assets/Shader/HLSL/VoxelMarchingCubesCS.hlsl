#include "Common.hlsli"
#include "HelperFunctions.hlsli"
#include "VoxelTerrainCommon.hlsli"

cbuffer McPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_CellsPerAxis;   // C; volume dim = C + 1 + 2A
    uint  g_Apron;          // A apron texels per side
    float g_VoxelSizeMeter;
    uint  g_VertexSlabBase; // this chunk's base offset into the vertex pool (in vertices)
    uint  g_MaxTriangles;   // slab capacity in triangles -- overflow guard
    float g_GeomorphRadiusMeter;
};

ConstantBuffer< DescriptorHeapIndex > g_DensityField : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MCCounter    : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutVertices  : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TriTable     : register(b4, ROOT_CONSTANT_SPACE);

static StructuredBuffer<float> Density  = GetResource(g_DensityField.index);
static StructuredBuffer<int>   TriTable = GetResource(g_TriTable.index);

// Isosurface vertex on `edge`: position interpolates the two corner samples along the density gradient
VoxelVertex MakeEdgeVertex(int edge, float cornerVal[8], float3 cornerPos[8], float3 cornerGrad[8], float chunkSizeMeter)
{
    uint2  ec = kEdgeCorners[edge];
    float3 pos, n;
    VoxelEdgePoint(cornerVal[ec.x], cornerVal[ec.y], cornerPos[ec.x], cornerPos[ec.y], cornerGrad[ec.x], cornerGrad[ec.y], pos, n);

    // Should use the exact same normal value(decoded) as the rendering point(GBuffer) for consistency
    float3 nq = VoxelQuantizeNormal(n);

    VoxelTerrainGenParams gp = (VoxelTerrainGenParams) 0;
    gp.cellsPerAxis   = g_CellsPerAxis;
    gp.apron          = g_Apron;
    gp.voxelSizeMeter = g_VoxelSizeMeter;
    VoxelProjectResult pr1 = VoxelProjectStage(gp, pos, nq, g_GeomorphRadiusMeter, 2u, Density, TriTable);       // s1 = hit point on parent mesh
    VoxelProjectResult pr2 = VoxelProjectStage(gp, pos, nq, 2.0 * g_GeomorphRadiusMeter, 4u, Density, TriTable); // s2 = hit point on grandparent
    VoxelProjectResult pr3 = VoxelProjectStage(gp, pos, nq, 4.0 * g_GeomorphRadiusMeter, 8u, Density, TriTable); // s3 = hit point on great-grandparent

    float s1 = pr1.sd;
    float s2 = (pr2.code == 0u) ? pr2.sd : s1; // no hit: stay on the previous LOD
    float s3 = (pr3.code == 0u) ? pr3.sd : s2;

    // normal morph targets: the ancestors' shading normals where the ray hit
    float3 nT1 = pr1.nTarget;
    float3 nT2 = (pr2.code == 0u) ? pr2.nTarget : nT1;

    return VoxelPackVertex(pos, nq, nT1, nT2, s1, s2, s3, false, chunkSizeMeter, g_VoxelSizeMeter);
}

[numthreads(4, 4, 4)]
void main(uint3 cell : SV_DispatchThreadID)
{
    const uint C = g_CellsPerAxis;
    if (cell.x >= C || cell.y >= C || cell.z >= C)
        return;

    const uint dim = C + 1u + 2u * g_Apron;

    // sample 8 corners + a central-difference gradient at each (apron guarantees the neighbours)
    float  cornerVal[8];
    float3 cornerPos[8];
    float3 cornerGrad[8];
    uint   cubeIndex = 0u;
    [unroll] for (uint i = 0u; i < 8u; ++i)
    {
        uint3 gc = cell + kCornerOffset[i];
        uint3 tx = gc + g_Apron;
        float d  = Density[FlatTexel(tx, dim)];

        cornerVal[i]  = d;
        cornerPos[i]  = float3(gc) * g_VoxelSizeMeter;
        cornerGrad[i] = float3(
            Density[FlatTexel(tx + uint3(1u, 0u, 0u), dim)] - Density[FlatTexel(tx - uint3(1u, 0u, 0u), dim)],
            Density[FlatTexel(tx + uint3(0u, 1u, 0u), dim)] - Density[FlatTexel(tx - uint3(0u, 1u, 0u), dim)],
            Density[FlatTexel(tx + uint3(0u, 0u, 1u), dim)] - Density[FlatTexel(tx - uint3(0u, 0u, 1u), dim)]);

        if (d < 0.0)
            cubeIndex |= (1u << i); // solid corner sets the bit
    }

    if (cubeIndex == 0u || cubeIndex == 255u)
        return; // fully inside/outside -> no surface

    // triangle count = tri-table entries until the -1 terminator
    uint triCount = 0u;
    [loop] for (uint e = 0u; e < 15u; e += 3u)
    {
        if (TriTable[cubeIndex * 16u + e] < 0)
            break;
        ++triCount;
    }

    RWByteAddressBuffer Counter = GetResource(g_MCCounter.index);
    uint baseTri;
    Counter.InterlockedAdd(0u, triCount, baseTri); // reserve a contiguous triangle range
    Counter.InterlockedAdd(4u, 1u);                // active-cell tally

    const float chunkSize = float(C) * g_VoxelSizeMeter;

    RWStructuredBuffer< VoxelVertex > OutV = GetResource(g_OutVertices.index);
    for (uint t = 0u; t < triCount; ++t)
    {
        if (baseTri + t >= g_MaxTriangles)
            break;

        int eA = TriTable[cubeIndex * 16u + t * 3u + 0u];
        int eB = TriTable[cubeIndex * 16u + t * 3u + 1u];
        int eC = TriTable[cubeIndex * 16u + t * 3u + 2u];

        uint v0 = g_VertexSlabBase + (baseTri + t) * 3u;
        OutV[v0 + 0u] = MakeEdgeVertex(eA, cornerVal, cornerPos, cornerGrad, chunkSize);
        OutV[v0 + 1u] = MakeEdgeVertex(eC, cornerVal, cornerPos, cornerGrad, chunkSize); // winding order: eA, eC, eB
        OutV[v0 + 2u] = MakeEdgeVertex(eB, cornerVal, cornerPos, cornerGrad, chunkSize);
    }
}
