#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"

struct VoxelEdgeArgsPush
{
    VoxelExtractConstants constants;

    uint ID_MCCounter;
    uint ID_EdgeArgs;
};

[[vk::push_constant]] ConstantBuffer< VoxelEdgeArgsPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Freezes E = active edges that own a vertex slot, and sizes the vertex pass
[numthreads(1, 1, 1)]
void main()
{
    RWStructuredBuffer< uint > counter = GetResource(g_Push.ID_MCCounter);
    uint E = min(counter[kVoxelCounterVertex], g_Push.constants.maxVertices);
    counter[kVoxelCounterEdgeVertex] = E;

    RWStructuredBuffer< uint > args = GetResource(g_Push.ID_EdgeArgs);
    args[0] = (E + 63u) / 64u; // one 64-thread edge-vertex group per 64 list entries
    args[1] = 1u;
    args[2] = 1u;
}
