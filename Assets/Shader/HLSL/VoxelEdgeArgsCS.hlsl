#include "Common.hlsli"

cbuffer ExtractPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_CellsPerAxis;   // C; volume dim = C + 1 + 2A
    uint  g_Apron;          // A apron texels per side
    float g_VoxelSizeMeter;
    uint  g_VertexSlabBase; // this chunk's base offset into the vertex pool (in vertices)
    uint  g_MaxTriangles;
    float g_GeomorphRadiusMeter;
    uint  g_MaxVertices;
};

ConstantBuffer< DescriptorHeapIndex > g_MCCounter : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_EdgeArgs  : register(b2, ROOT_CONSTANT_SPACE);

[numthreads(1, 1, 1)]
void main()
{
    RWByteAddressBuffer Counter = GetResource(g_MCCounter.index);
    uint E = min(Counter.Load(8u), g_MaxVertices); // active edges that own a vertex slot
    Counter.Store(16u, E);

    RWStructuredBuffer< uint > Args = GetResource(g_EdgeArgs.index);
    Args[0] = (E + 63u) / 64u; // one 64-thread edge-vertex group per 64 list entries
    Args[1] = 1u;
    Args[2] = 1u;
}
