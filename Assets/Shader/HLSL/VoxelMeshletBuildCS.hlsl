#include "Common.hlsli"

cbuffer MeshletBuildPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_ChunkID;             // chunk-table row to patch
    uint g_MeshletSlabBase;
    uint g_TrianglesPerMeshlet; // K; 3*K <= MS maxvertices
    uint g_MaxMeshlets;
    uint g_MaxTriangles;
};

ConstantBuffer< DescriptorHeapIndex > g_MCCounter   : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutMeshlets : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutChunks   : register(b3, ROOT_CONSTANT_SPACE);

[numthreads(64, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    uint m = dt.x;
    if (m >= g_MaxMeshlets)
        return;

    RWByteAddressBuffer Counter = GetResource(g_MCCounter.index);
    // clamp to slab capacity -- guards every downstream count against a runaway extract
    uint totalTris   = min(Counter.Load(0u), g_MaxTriangles);
    uint K           = g_TrianglesPerMeshlet;
    uint numMeshlets = min((totalTris + K - 1u) / K, g_MaxMeshlets);

    if (m == 0u) // lane 0 publishes the GPU-driven counts to the chunk row
    {
        RWStructuredBuffer< VoxelChunk > Chunks = GetResource(g_OutChunks.index);
        Chunks[g_ChunkID].vertexCount  = totalTris * 3u;
        Chunks[g_ChunkID].meshletCount = numMeshlets;
    }

    if (m >= numMeshlets)
        return;

    uint triBase  = m * K;
    uint triCount = min(K, totalTris - triBase);

    Meshlet ml;
    ml.vertexOffset   = triBase * 3u; // 3 verts/triangle
    ml.triangleOffset = triBase;
    ml.vertexCount    = triCount * 3u;
    ml.triangleCount  = triCount;
    ml.centerX = 0.0; ml.centerY = 0.0; ml.centerZ = 0.0; // bounds/cone unused (chunk-level cull only)
    ml.radius  = 0.0;
    ml.coneAxisX = 0.0; ml.coneAxisY = 0.0; ml.coneAxisZ = 0.0;
    ml.coneCutoff = 1.0;

    RWStructuredBuffer< Meshlet > OutM = GetResource(g_OutMeshlets.index);
    OutM[g_MeshletSlabBase + m] = ml;
}
