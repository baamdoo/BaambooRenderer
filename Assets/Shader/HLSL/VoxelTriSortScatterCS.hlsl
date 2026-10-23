#include "Common.hlsli"
#include "HelperFunctions.hlsli"
#include "VoxelTerrainCommon.hlsli"

cbuffer TriSortPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_VertexSlabBase;
    uint  g_MaxTriangles;
};

ConstantBuffer< DescriptorHeapIndex > g_MCCounter         : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Vertices          : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SortBins          : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutSortedTriplets : register(b4, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Triplets          : register(b5, ROOT_CONSTANT_SPACE);

[numthreads(256, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    uint t = dt.x;

    RWByteAddressBuffer Counter = GetResource(g_MCCounter.index);
    uint totalTris = min(Counter.Load(0u), g_MaxTriangles);
    if (t >= totalTris)
        return;

    StructuredBuffer< VoxelVertex > Verts = GetResource(g_Vertices.index);
    StructuredBuffer< uint >        Tri   = GetResource(g_Triplets.index);
    uint3 posQSum = uint3(0u, 0u, 0u);
    [unroll] for (uint k = 0u; k < 3u; ++k)
    {
        VoxelVertex v = Verts[g_VertexSlabBase + Tri[t * 3u + k]];
        posQSum += uint3(v.posXY >> 16u, v.posXY & 0xFFFFu, v.posZnormal >> 16u);
    }

    uint key = VoxelTriSortKey(posQSum);

    RWStructuredBuffer< uint > Bins = GetResource(g_SortBins.index);
    uint dst;
    InterlockedAdd(Bins[key], 1u, dst);
    if (dst >= totalTris) // tri binned differently than in Count: no slot left in the sorted range
        return;

    RWStructuredBuffer< uint > OutSorted = GetResource(g_OutSortedTriplets.index);
    uint d = dst * 3u;
    OutSorted[d + 0u] = Tri[t * 3u + 0u]; // page-relative vertex numbers, winding order kept
    OutSorted[d + 1u] = Tri[t * 3u + 1u];
    OutSorted[d + 2u] = Tri[t * 3u + 2u];
}
