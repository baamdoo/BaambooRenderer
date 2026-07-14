#include "Common.hlsli"

cbuffer MeshletBuildPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_ChunkID;
    uint g_MeshletSlabBase;
    uint g_TrianglesPerMeshlet;    // 3*K <= mesh shader max vertices
    uint g_MaxMeshlets;
    uint g_MaxTriangles;
    uint g_VertexSlabBase;
    uint g_MeshletVertexSlabBase;
};

ConstantBuffer< DescriptorHeapIndex > g_MCCounter    : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutMeshlets  : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutCounts    : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Vertices     : register(b4, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MeshletVerts : register(b5, ROOT_CONSTANT_SPACE);

[numthreads(64, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    uint m = dt.x;
    if (m >= g_MaxMeshlets)
        return;

    RWByteAddressBuffer Counter = GetResource(g_MCCounter.index);
    // clamp to slab capacity
    uint totalTris   = min(Counter.Load(0u), g_MaxTriangles);
    uint K           = g_TrianglesPerMeshlet;
    uint numMeshlets = min((totalTris + K - 1u) / K, g_MaxMeshlets);

    if (m == 0u) // thread 0 publishes the meshlet count to the chunk's counts row
    {
        RWStructuredBuffer< VoxelChunkCounts > Counts = GetResource(g_OutCounts.index);
        Counts[g_ChunkID].meshletCount = numMeshlets;
    }

    if (m >= numMeshlets)
        return;

    uint triBase  = m * K;
    uint triCount = min(K, totalTris - triBase);

    // AABB over meshlet verts, read through the sorted meshlet-vertex indirection (vertex pool stays in MC order)
    StructuredBuffer< Vertex > Verts        = GetResource(g_Vertices.index);
    StructuredBuffer< uint >   MeshletVerts = GetResource(g_MeshletVerts.index);
    float3 bmin = float3(1e30, 1e30, 1e30);
    float3 bmax = float3(-1e30, -1e30, -1e30);
    for (uint v = 0u; v < triCount * 3u; ++v)
    {
        uint   vLocal = MeshletVerts[g_MeshletVertexSlabBase + triBase * 3u + v];
        Vertex vv     = Verts[g_VertexSlabBase + vLocal];
        float3 p  = float3(vv.posX, vv.posY, vv.posZ);
        bmin = min(bmin, p);
        bmax = max(bmax, p);
    }
    float3 center = 0.5 * (bmin + bmax);

    Meshlet meshlet;
    meshlet.vertexOffset   = triBase * 3u;
    meshlet.triangleOffset = triBase;
    meshlet.vertexCount    = triCount * 3u;
    meshlet.triangleCount  = triCount;
    meshlet.centerX        = center.x; meshlet.centerY = center.y; meshlet.centerZ = center.z;
    meshlet.radius         = length(bmax - center);
    meshlet.coneAxisX      = 0.0; meshlet.coneAxisY = 0.0; meshlet.coneAxisZ = 0.0; // cone cull unused
    meshlet.coneCutoff     = 1.0;

    RWStructuredBuffer< Meshlet > OutM = GetResource(g_OutMeshlets.index);
    OutM[g_MeshletSlabBase + m] = meshlet;
}
