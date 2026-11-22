#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/VoxelMeshing.slangh"

struct VoxelMarchingCubesPush
{
    VoxelExtractConstants constants;

    uint ID_TriTable;
    uint ID_DensityField;
    uint ID_MCCounter;
    uint ID_EdgeMap;
    uint ID_Triplets;
};

[[vk::push_constant]] ConstantBuffer< VoxelMarchingCubesPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// edge -> (min corner = owner grid point offset, max corner, axis x:0 y:1 z:2)
static const uint3 kEdgeOwner[12] =
{
    uint3(0, 1, 0), uint3(1, 2, 1), uint3(3, 2, 0), uint3(0, 3, 1),
    uint3(4, 5, 0), uint3(5, 6, 1), uint3(7, 6, 0), uint3(4, 7, 1),
    uint3(0, 4, 2), uint3(1, 5, 2), uint3(2, 6, 2), uint3(3, 7, 2)
};

// Each active cell appends its triangles as triplets of page-relative vertex numbers
[numthreads(4, 4, 4)]
void main(uint3 cell : SV_DispatchThreadID)
{
    const VoxelExtractConstants ex = g_Push.constants;

    const uint C = ex.cellsPerAxis;
    if (cell.x >= C || cell.y >= C || cell.z >= C)
        return;

    const uint dim = C + 1u + 2u * ex.apron;

    StructuredBuffer< float > density  = GetResource(g_Push.ID_DensityField);
    StructuredBuffer< int >   triTable = GetResource(g_Push.ID_TriTable);

    uint cubeIndex = 0u;
    [unroll] for (uint i = 0u; i < 8u; ++i)
    {
        uint3 tx = cell + kCornerOffset[i] + ex.apron;
        if (density[FlatTexel(tx, dim)] < 0.0)
            cubeIndex |= (1u << i); // solid corner sets the bit
    }

    if (cubeIndex == 0u || cubeIndex == 255u)
        return; // fully inside/outside -> no surface

    // triangle count = tri-table entries until the -1 terminator
    uint triCount = 0u;
    [loop] for (uint e = 0u; e < 15u; e += 3u)
    {
        if (triTable[cubeIndex * 16u + e] < 0)
            break;
        ++triCount;
    }

    RWStructuredBuffer< uint > counter = GetResource(g_Push.ID_MCCounter);
    InterlockedAdd(counter[kVoxelCounterActiveCell], 1u);

    // Look up every triangle's 3 vertex numbers at the owner grid points of its edges
    StructuredBuffer< uint > edgeMap = GetResource(g_Push.ID_EdgeMap);
    uint nums[15];
    uint validMask  = 0u;
    uint validCount = 0u;
    [unroll] for (uint t = 0u; t < 5u; ++t)
    {
        if (t < triCount)
        {
            int edges[3] =
            {
                triTable[cubeIndex * 16u + t * 3u + 0u],
                triTable[cubeIndex * 16u + t * 3u + 2u],
                triTable[cubeIndex * 16u + t * 3u + 1u]
            }; // winding order: eA, eC, eB

            bool valid = true;
            [unroll] for (uint k = 0u; k < 3u; ++k)
            {
                uint3 eo    = kEdgeOwner[edges[k]]; // (owner corner, far corner, axis)
                uint3 owner = cell + kCornerOffset[eo.x];
                uint  n     = edgeMap[FlatTexel(owner, C + 1u) * 3u + eo.z];

                nums[t * 3u + k] = n;
                valid = valid && (n < ex.maxVertices); // sentinel and vertex overflow both fail
            }
            if (valid)
            {
                validMask |= 1u << t;
                ++validCount;
            }
        }
    }

    uint baseTri;
    InterlockedAdd(counter[kVoxelCounterTri], validCount, baseTri); // reserve only triangles whose 3 vertices exist

    // Pack the kept triangles contiguously from baseTri
    RWStructuredBuffer< uint > triplets = GetResource(g_Push.ID_Triplets);
    uint s = baseTri;
    [unroll] for (uint t = 0u; t < 5u; ++t)
    {
        if (((validMask >> t) & 1u) != 0u)
        {
            if (s < ex.maxTriangles)
            {
                triplets[s * 3u + 0u] = nums[t * 3u + 0u];
                triplets[s * 3u + 1u] = nums[t * 3u + 1u];
                triplets[s * 3u + 2u] = nums[t * 3u + 2u];
            }
            ++s;
        }
    }
}
