#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/VoxelTerrain.slangh"

struct VoxelEdgeMarkPush
{
    VoxelExtractConstants constants;

    uint ID_DensityField;
    uint ID_MCCounter;
    uint ID_EdgeMap;
    uint ID_EdgeList;
};

[[vk::push_constant]] ConstantBuffer< VoxelEdgeMarkPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Each grid point numbers its active +x/+y/+z edges into the edge map and the compact edge list
[numthreads(4, 4, 4)]
void main(uint3 tID : SV_DispatchThreadID)
{
    const uint C = g_Push.constants.cellsPerAxis;
    if (tID.x > C || tID.y > C || tID.z > C)
        return;

    const uint dim = C + 1u + 2u * g_Push.constants.apron;

    StructuredBuffer< float > density = GetResource(g_Push.ID_DensityField);

    uint3 tx = tID + g_Push.constants.apron;
    float d0 = density[FlatTexel(tx, dim)];
    float dx = density[FlatTexel(tx + uint3(1u, 0u, 0u), dim)];
    float dy = density[FlatTexel(tx + uint3(0u, 1u, 0u), dim)];
    float dz = density[FlatTexel(tx + uint3(0u, 0u, 1u), dim)];

    bool s0 = d0 < 0.0;
    bool sx = dx < 0.0;
    bool sy = dy < 0.0;
    bool sz = dz < 0.0;

    RWStructuredBuffer< uint > counter  = GetResource(g_Push.ID_MCCounter);
    RWStructuredBuffer< uint > edgeMap  = GetResource(g_Push.ID_EdgeMap);
    RWStructuredBuffer< uint > edgeList = GetResource(g_Push.ID_EdgeList);

    const uint maxVertices = g_Push.constants.maxVertices;
    const uint keyBase     = FlatTexel(tID, C + 1u) * 3u;

    if (tID.x < C)
    {
        uint key = keyBase + 0u;
        uint vi  = kInvalidIndex;
        if (s0 != sx) // active edge
        {
            InterlockedAdd(counter[kVoxelCounterVertex], 1u, vi);
            if (vi < maxVertices)
                edgeList[vi] = key;
            else
                vi = kInvalidIndex;
        }
        edgeMap[key] = vi;
    }

    if (tID.y < C)
    {
        uint key = keyBase + 1u;
        uint vi  = kInvalidIndex;
        if (s0 != sy) // active edge
        {
            InterlockedAdd(counter[kVoxelCounterVertex], 1u, vi);
            if (vi < maxVertices)
                edgeList[vi] = key;
            else
                vi = kInvalidIndex;
        }
        edgeMap[key] = vi;
    }

    if (tID.z < C)
    {
        uint key = keyBase + 2u;
        uint vi  = kInvalidIndex;
        if (s0 != sz) // active edge
        {
            InterlockedAdd(counter[kVoxelCounterVertex], 1u, vi);
            if (vi < maxVertices)
                edgeList[vi] = key;
            else
                vi = kInvalidIndex;
        }
        edgeMap[key] = vi;
    }
}
