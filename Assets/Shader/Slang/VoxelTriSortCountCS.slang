#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/VoxelMeshing.slangh"

struct VoxelTriSortCountPush
{
    VoxelTriSortConstants constants;

    uint ID_MCCounter;
    uint ID_Vertices;
    uint ID_SortBins;
    uint ID_Triplets;
};

[[vk::push_constant]] ConstantBuffer< VoxelTriSortCountPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Histogram of the triangles over the Morton sort blocks
[numthreads(256, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    uint t = dt.x;

    RWStructuredBuffer< uint > counter = GetResource(g_Push.ID_MCCounter);
    uint totalTris = min(counter[kVoxelCounterTri], g_Push.constants.maxTriangles); // clamp to page capacity
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
    InterlockedAdd(bins[key], 1u);
}
