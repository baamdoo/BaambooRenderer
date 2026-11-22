#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/VoxelMeshing.slangh"

struct VoxelTriSortScatterPush
{
    VoxelTriSortConstants constants;

    uint ID_MCCounter;
    uint ID_Vertices;
    uint ID_SortBins;
    uint ID_OutSortedTriplets;
    uint ID_Triplets;
};

[[vk::push_constant]] ConstantBuffer< VoxelTriSortScatterPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Moves each triplet to its slot in Morton-block order; the scanned bins serve as cursors
[numthreads(256, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    uint t = dt.x;

    RWStructuredBuffer< uint > counter = GetResource(g_Push.ID_MCCounter);
    uint totalTris = min(counter[kVoxelCounterTri], g_Push.constants.maxTriangles);
    if (t >= totalTris)
        return;

    StructuredBuffer< VoxelVertex > verts = GetResource(g_Push.ID_Vertices);
    StructuredBuffer< uint >        tri   = GetResource(g_Push.ID_Triplets);
    uint3 posQSum = uint3(0u, 0u, 0u);
    [unroll] for (uint k = 0u; k < 3u; ++k)
    {
        VoxelVertex v = verts[g_Push.constants.vertexPageBase + tri[t * 3u + k]];
        posQSum += uint3(v.posXY >> 16u, v.posXY & 0xFFFFu, v.posZnormal >> 16u);
    }

    uint key = VoxelTriSortKey(posQSum);

    RWStructuredBuffer< uint > bins = GetResource(g_Push.ID_SortBins);
    uint dst;
    InterlockedAdd(bins[key], 1u, dst);
    if (dst >= totalTris) // tri binned differently than in Count: no slot left in the sorted range
        return;

    RWStructuredBuffer< uint > outSorted = GetResource(g_Push.ID_OutSortedTriplets);
    uint d = dst * 3u;
    outSorted[d + 0u] = tri[t * 3u + 0u]; // page-relative vertex numbers, winding order kept
    outSorted[d + 1u] = tri[t * 3u + 1u];
    outSorted[d + 2u] = tri[t * 3u + 2u];
}
