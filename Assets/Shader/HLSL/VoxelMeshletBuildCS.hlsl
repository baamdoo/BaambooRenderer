#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"
#include "VoxelTerrainCommon.hlsli"

cbuffer MeshletBuildPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_ChunkID;
    uint  g_MeshletSlabBase;
    uint  g_TrianglesPerMeshlet;   // 3*K <= mesh shader max vertices
    uint  g_MaxMeshlets;
    uint  g_MaxTriangles;
    uint  g_VertexSlabBase;
    uint  g_MeshletVertexSlabBase;
    float g_ChunkSizeMeter;
    uint  g_MeshletTriSlabBase;
    uint  g_MaxVertices;
    uint  g_MaxMeshletVertices;
};

ConstantBuffer< DescriptorHeapIndex > g_MCCounter       : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutMeshlets     : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutCounts       : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Vertices        : register(b4, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutMeshletVerts : register(b5, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutMeshletTris  : register(b6, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SortedTriplets  : register(b7, ROOT_CONSTANT_SPACE);

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

    // Dedup the window's sorted triplets: global vertex number -> meshlet-local index in first-appearance order
    StructuredBuffer< uint > Sorted = GetResource(g_SortedTriplets.index);
    uint vLimit = min(Counter.Load(8u), g_MaxVertices); // vertex numbers that were actually written

    uint uniq[3u * VOXEL_TRIS_PER_MESHLET];   // meshlet-local index -> global vertex number
    uint localTris[VOXEL_TRIS_PER_MESHLET];   // kept triangle -> packed local triplet
    uint uniqCount = 0u;
    uint keptCount = 0u;
    for (uint i = 0u; i < triCount; ++i)
    {
        uint tri[3] = { Sorted[(triBase + i) * 3u + 0u], Sorted[(triBase + i) * 3u + 1u], Sorted[(triBase + i) * 3u + 2u] };
        if (tri[0] >= vLimit || tri[1] >= vLimit || tri[2] >= vLimit)
            continue; // skip triangles that reference a vertex this build did not write

        uint packed = 0u;
        [unroll] for (uint k = 0u; k < 3u; ++k)
        {
            uint local = uniqCount;
            for (uint j = 0u; j < uniqCount; ++j)
            {
                if (uniq[j] == tri[k])
                {
                    local = j;
                    break;
                }
            }
            if (local == uniqCount)
                uniq[uniqCount++] = tri[k];
            packed |= local << (8u * k); // winding order kept: slot k of the triplet
        }
        localTris[keptCount++] = packed;
    }

    uint mvBase;
    Counter.InterlockedAdd(12u, uniqCount, mvBase); // reserve this meshlet's slice of the meshlet-vertex pool
    bool emit = keptCount > 0u && mvBase + uniqCount <= g_MaxMeshletVertices;

    // Write the vertex indirection, the local triplets and the bounds over the unique vertices
    float3 bmin = float3(1e30, 1e30, 1e30);
    float3 bmax = float3(-1e30, -1e30, -1e30);
    if (emit)
    {
        StructuredBuffer< VoxelVertex > Verts = GetResource(g_Vertices.index);
        RWStructuredBuffer< uint >      OutMV = GetResource(g_OutMeshletVerts.index);
        RWStructuredBuffer< uint >      OutMT = GetResource(g_OutMeshletTris.index);
        for (uint v = 0u; v < uniqCount; ++v)
        {
            OutMV[g_MeshletVertexSlabBase + mvBase + v] = uniq[v];

            float3 p = VoxelUnpackPos(Verts[g_VertexSlabBase + uniq[v]], g_ChunkSizeMeter);
            bmin = min(bmin, p);
            bmax = max(bmax, p);
        }
        for (uint t = 0u; t < keptCount; ++t)
            OutMT[g_MeshletTriSlabBase + triBase + t] = localTris[t];
    }
    float3 center = emit ? 0.5 * (bmin + bmax) : float3(0.0, 0.0, 0.0);

    Meshlet meshlet;
    meshlet.vertexOffset   = emit ? mvBase : 0u;
    meshlet.triangleOffset = triBase;
    meshlet.vertexCount    = emit ? uniqCount : 0u;
    meshlet.triangleCount  = emit ? keptCount : 0u; // empty meshlet: the mesh shader emits nothing
    meshlet.centerX        = center.x; meshlet.centerY = center.y; meshlet.centerZ = center.z;
    meshlet.radius         = emit ? length(bmax - center) + g_ChunkSizeMeter * (1.0 / 128.0) : 0.0; // transition delta can leave primary bounds by up to a cell
    meshlet.coneAxisX      = 0.0; meshlet.coneAxisY = 0.0; meshlet.coneAxisZ = 0.0; // cone cull unused
    meshlet.coneCutoff     = 1.0;

    RWStructuredBuffer< Meshlet > OutM = GetResource(g_OutMeshlets.index);
    OutM[g_MeshletSlabBase + m] = meshlet;
}
