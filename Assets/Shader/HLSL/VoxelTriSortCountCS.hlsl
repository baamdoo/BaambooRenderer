#include "Common.hlsli"
#include "HelperFunctions.hlsli"

cbuffer TriSortPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_VertexSlabBase;
    uint  g_MeshletVertexSlabBase;
    uint  g_MaxTriangles;
    float g_ChunkSizeMeter;
};

ConstantBuffer< DescriptorHeapIndex > g_MCCounter : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Vertices  : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SortBins  : register(b3, ROOT_CONSTANT_SPACE);

[numthreads(256, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    uint t = dt.x;

    RWByteAddressBuffer Counter = GetResource(g_MCCounter.index);
    uint totalTris = min(Counter.Load(0u), g_MaxTriangles); // clamp to slab capacity
    if (t >= totalTris)
        return;

    StructuredBuffer< Vertex > Verts = GetResource(g_Vertices.index);
    float3 c = float3(0.0, 0.0, 0.0);
    [unroll] for (uint k = 0u; k < 3u; ++k)
    {
        Vertex vv = Verts[g_VertexSlabBase + t * 3u + k];
        c += float3(vv.posX, vv.posY, vv.posZ);
    }

    uint key = VoxelTriSortKey(c / 3.0, g_ChunkSizeMeter);

    RWStructuredBuffer< uint > Bins = GetResource(g_SortBins.index);
    InterlockedAdd(Bins[key], 1u);
}
