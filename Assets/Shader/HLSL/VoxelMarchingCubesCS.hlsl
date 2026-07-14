#include "Common.hlsli"

cbuffer McPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_CellsPerAxis;   // C; volume dim = C + 1 + 2A
    uint  g_Apron;          // A apron texels per side
    float g_VoxelSizeMeter;
    uint  g_VertexSlabBase; // this chunk's base offset into the vertex pool (in vertices)
    uint  g_MaxTriangles;   // slab capacity in triangles -- overflow guard
};

ConstantBuffer< DescriptorHeapIndex > g_DensityField : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MCCounter    : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutVertices  : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TriTable     : register(b4, ROOT_CONSTANT_SPACE);

// corner i -> unit-cube offset
static const uint3 kCornerOffset[8] =
{
    uint3(0, 0, 0), uint3(1, 0, 0), uint3(1, 1, 0), uint3(0, 1, 0),
    uint3(0, 0, 1), uint3(1, 0, 1), uint3(1, 1, 1), uint3(0, 1, 1)
};

// edge -> its two corners
static const uint2 kEdgeCorners[12] =
{
    uint2(0, 1), uint2(1, 2), uint2(2, 3), uint2(3, 0),
    uint2(4, 5), uint2(5, 6), uint2(6, 7), uint2(7, 4),
    uint2(0, 4), uint2(1, 5), uint2(2, 6), uint2(3, 7)
};

uint FlatTexel(uint3 t, uint dim)
{
    return (t.z * dim + t.y) * dim + t.x;
}

// Isosurface vertex on `edge`: position interpolates the two corner samples, normal = density gradient.
Vertex MakeEdgeVertex(int edge, float cornerVal[8], float3 cornerPos[8], float3 cornerGrad[8])
{
    uint2  ec = kEdgeCorners[edge];
    float  v0 = cornerVal[ec.x],  v1 = cornerVal[ec.y];
    float3 p0 = cornerPos[ec.x],  p1 = cornerPos[ec.y];
    float3 g0 = cornerGrad[ec.x], g1 = cornerGrad[ec.y];

    float  tt  = (abs(v0 - v1) < 1e-6) ? 0.0 : v0 / (v0 - v1); // zero crossing along the edge
    float3 pos = lerp(p0, p1, tt);
    float3 g   = lerp(g0, g1, tt);
    float  gl2 = dot(g, g);
    float3 n   = (gl2 > 1e-12) ? g * rsqrt(gl2) : float3(0.0, 1.0, 0.0);

    Vertex vert;
    vert.posX = pos.x; vert.posY = pos.y; vert.posZ = pos.z;
    vert.u = 0.0; vert.v = 0.0;
    vert.normalX = n.x; vert.normalY = n.y; vert.normalZ = n.z;
    vert.tangentX = 0.0; vert.tangentY = 0.0; vert.tangentZ = 0.0;
    return vert;
}

[numthreads(4, 4, 4)]
void main(uint3 cell : SV_DispatchThreadID)
{
    const uint C = g_CellsPerAxis;
    if (cell.x >= C || cell.y >= C || cell.z >= C)
        return;

    const uint dim = C + 1u + 2u * g_Apron;
    StructuredBuffer< float > Density  = GetResource(g_DensityField.index);
    StructuredBuffer< int >   TriTable = GetResource(g_TriTable.index);

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

    if (baseTri + triCount > g_MaxTriangles)
        return; // slab overflow -> drop

    RWStructuredBuffer< Vertex > OutV = GetResource(g_OutVertices.index);
    for (uint t = 0u; t < triCount; ++t)
    {
        int eA = TriTable[cubeIndex * 16u + t * 3u + 0u];
        int eB = TriTable[cubeIndex * 16u + t * 3u + 1u];
        int eC = TriTable[cubeIndex * 16u + t * 3u + 2u];

        uint v0 = g_VertexSlabBase + (baseTri + t) * 3u;
        OutV[v0 + 0u] = MakeEdgeVertex(eA, cornerVal, cornerPos, cornerGrad);
        OutV[v0 + 1u] = MakeEdgeVertex(eC, cornerVal, cornerPos, cornerGrad); // winding order: eA, eC, eB
        OutV[v0 + 2u] = MakeEdgeVertex(eB, cornerVal, cornerPos, cornerGrad);
    }
}
