#include "ShaderShared/VoxelTerrain.h"
#include "ShaderShared/Mesh.h"
#include "Common/Binding.slangh"
#include "Common/VoxelTerrain.slangh"

struct VoxelMeshletBuildPush
{
    VoxelMeshletBuildConstants constants;

    uint ID_MCCounter;
    uint ID_OutMeshlets;
    uint ID_OutCounts;
    uint ID_Vertices;
    uint ID_OutMeshletVerts;
    uint ID_OutMeshletTris;
    uint ID_SortedTriplets;
};

[[vk::push_constant]] ConstantBuffer< VoxelMeshletBuildPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Packs sequential meshlets of kVoxelTrianglesPerMeshlet sorted triangles and publishes the chunk's meshlet count
[numthreads(64, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    const VoxelMeshletBuildConstants mb = g_Push.constants;

    uint m = dt.x;
    if (m >= mb.maxMeshlets)
        return;

    RWStructuredBuffer< uint > counter = GetResource(g_Push.ID_MCCounter);
    // clamp to page capacity
    uint totalTris   = min(counter[kVoxelCounterTri], mb.maxTriangles);
    uint K           = kVoxelTrianglesPerMeshlet;
    uint numMeshlets = min((totalTris + K - 1u) / K, mb.maxMeshlets);

    if (m == 0u) // thread 0 publishes the meshlet count to the chunk's counts row
    {
        RWStructuredBuffer< VoxelChunkCounts > counts = GetResource(g_Push.ID_OutCounts);
        counts[mb.chunkID].meshletCount = numMeshlets;
    }

    if (m >= numMeshlets)
        return;

    uint triBase  = m * K;
    uint triCount = min(K, totalTris - triBase);

    // Dedup the window's sorted triplets: global vertex number -> meshlet-local index in first-appearance order
    StructuredBuffer< uint > sorted = GetResource(g_Push.ID_SortedTriplets);
    uint vLimit = min(counter[kVoxelCounterVertex], mb.maxVertices); // vertex numbers that were actually written

    uint uniq[3u * kVoxelTrianglesPerMeshlet] = {}; // meshlet-local index -> global vertex number
    uint localTris[kVoxelTrianglesPerMeshlet]; // kept triangle -> packed local triplet
    uint uniqCount = 0u;
    uint keptCount = 0u;
    for (uint i = 0u; i < triCount; ++i)
    {
        uint tri[3] = { sorted[(triBase + i) * 3u + 0u], sorted[(triBase + i) * 3u + 1u], sorted[(triBase + i) * 3u + 2u] };
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
    InterlockedAdd(counter[kVoxelCounterMeshletVertex], uniqCount, mvBase); // reserve this meshlet's slice of the meshlet-vertex pool
    bool emit = keptCount > 0u && mvBase + uniqCount <= mb.maxMeshletVertices;

    // Write the vertex indirection, the local triplets and the bounds over the unique vertices
    float3 bmin = float3(1e30, 1e30, 1e30);
    float3 bmax = float3(-1e30, -1e30, -1e30);
    if (emit)
    {
        StructuredBuffer< VoxelVertex > verts = GetResource(g_Push.ID_Vertices);
        RWStructuredBuffer< uint >      outMV = GetResource(g_Push.ID_OutMeshletVerts);
        RWStructuredBuffer< uint >      outMT = GetResource(g_Push.ID_OutMeshletTris);
        for (uint v = 0u; v < uniqCount; ++v)
        {
            outMV[mb.meshletVertexPageBase + mvBase + v] = uniq[v];

            float3 p = VoxelUnpackPos(verts[mb.vertexPageBase + uniq[v]], mb.chunkSizeMeter);
            bmin = min(bmin, p);
            bmax = max(bmax, p);
        }
        for (uint t = 0u; t < keptCount; ++t)
            outMT[mb.meshletTriPageBase + triBase + t] = localTris[t];
    }
    float3 center = emit ? 0.5 * (bmin + bmax) : float3(0.0, 0.0, 0.0);

    Meshlet meshlet;
    meshlet.vertexOffset   = emit ? mvBase : 0u;
    meshlet.triangleOffset = triBase;
    meshlet.vertexCount    = emit ? uniqCount : 0u;
    meshlet.triangleCount  = emit ? keptCount : 0u; // empty meshlet: the mesh shader emits nothing
    meshlet.center         = center;
    meshlet.radius         = emit ? length(bmax - center) + mb.chunkSizeMeter * (1.0 / 128.0) : 0.0; // transition delta can leave primary bounds by up to a cell
    meshlet.coneAxis       = float3(0.0, 0.0, 0.0); // cone cull unused
    meshlet.coneCutoff     = 1.0;

    RWStructuredBuffer< Meshlet > outMeshlets = GetResource(g_Push.ID_OutMeshlets);
    outMeshlets[mb.meshletPageBase + m] = meshlet;
}
