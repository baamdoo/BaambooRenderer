#include "Common.hlsli"
#include "HelperFunctions.hlsli"
#include "VoxelTerrainCommon.hlsli"

cbuffer McPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_CellsPerAxis;   // C; volume dim = C + 1 + 2A
    uint  g_Apron;          // A apron texels per side
    float g_VoxelSizeMeter;
    uint  g_VertexSlabBase; // this chunk's base offset into the vertex pool (in vertices)
    uint  g_MaxTriangles;
    float g_GeomorphRadiusMeter;
    uint  g_MaxVertices;
};

ConstantBuffer< DescriptorHeapIndex > g_DensityField : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MCCounter    : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TriTable     : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_EdgeMap      : register(b4, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Triplets     : register(b5, ROOT_CONSTANT_SPACE);

static StructuredBuffer< float > Density  = GetResource(g_DensityField.index);
static StructuredBuffer< int >   TriTable = GetResource(g_TriTable.index);

[numthreads(4, 4, 4)]
void main(uint3 cell : SV_DispatchThreadID)
{
    const uint C = g_CellsPerAxis;
    if (cell.x >= C || cell.y >= C || cell.z >= C)
        return;

    const uint dim = C + 1u + 2u * g_Apron;

    uint cubeIndex = 0u;
    [unroll] for (uint i = 0u; i < 8u; ++i)
    {
        uint3 tx = cell + kCornerOffset[i] + g_Apron;
        if (Density[FlatTexel(tx, dim)] < 0.0)
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
    Counter.InterlockedAdd(4u, 1u); // active-cell tally

    // Look up every triangle's 3 vertex numbers at the owner grid points of its edges
    StructuredBuffer< uint > EdgeMap = GetResource(g_EdgeMap.index);
    uint nums[15];
    uint validMask  = 0u;
    uint validCount = 0u;
    [unroll] for (uint t = 0u; t < 5u; ++t)
    {
        if (t < triCount)
        {
            int edges[3] =
            {
                TriTable[cubeIndex * 16u + t * 3u + 0u],
                TriTable[cubeIndex * 16u + t * 3u + 2u],
                TriTable[cubeIndex * 16u + t * 3u + 1u]
            }; // winding order: eA, eC, eB

            bool valid = true;
            [unroll] for (uint k = 0u; k < 3u; ++k)
            {
                uint3 eo    = kEdgeOwner[edges[k]]; // (owner corner, far corner, axis)
                uint3 owner = cell + kCornerOffset[eo.x];
                uint  n     = EdgeMap[FlatTexel(owner, C + 1u) * 3u + eo.z];

                nums[t * 3u + k] = n;
                valid = valid && (n < g_MaxVertices); // sentinel and vertex overflow both fail
            }
            if (valid)
            {
                validMask |= 1u << t;
                ++validCount;
            }
        }
    }

    uint baseTri;
    Counter.InterlockedAdd(0u, validCount, baseTri); // reserve only triangles whose 3 vertices exist

    // Pack the kept triangles contiguously from baseTri
    RWStructuredBuffer< uint > Triplets = GetResource(g_Triplets.index);
    uint s = baseTri;
    [unroll] for (uint t = 0u; t < 5u; ++t)
    {
        if ((validMask >> t) & 1u)
        {
            if (s < g_MaxTriangles)
            {
                Triplets[s * 3u + 0u] = nums[t * 3u + 0u];
                Triplets[s * 3u + 1u] = nums[t * 3u + 1u];
                Triplets[s * 3u + 2u] = nums[t * 3u + 2u];
            }
            ++s;
        }
    }
}
