#include "Common.hlsli"
#include "HelperFunctions.hlsli"

cbuffer TriSortPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_VertexSlabBase;
    uint  g_MeshletVertexSlabBase;
    uint  g_MaxTriangles;
    float g_ChunkSizeMeter;
};

ConstantBuffer< DescriptorHeapIndex > g_MCCounter       : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Vertices        : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SortBins        : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutMeshletVerts : register(b4, ROOT_CONSTANT_SPACE);

[numthreads(256, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    uint t = dt.x;

    RWByteAddressBuffer Counter = GetResource(g_MCCounter.index);
    uint totalTris = min(Counter.Load(0u), g_MaxTriangles);
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
    uint dst;
    InterlockedAdd(Bins[key], 1u, dst);

    RWStructuredBuffer< uint > OutMV = GetResource(g_OutMeshletVerts.index);
    uint d = g_MeshletVertexSlabBase + dst * 3u;
    uint s = t * 3u; // slab-relative; consumers add the vertex slab base
    OutMV[d + 0u] = s + 0u;
    OutMV[d + 1u] = s + 1u;
    OutMV[d + 2u] = s + 2u;
}
