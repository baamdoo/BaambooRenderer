#include "Common.hlsli"
#include "HelperFunctions.hlsli"
#include "VoxelTerrainCommon.hlsli"

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

ConstantBuffer< DescriptorHeapIndex > g_DensityField : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MCCounter    : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_EdgeMap      : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_EdgeList     : register(b4, ROOT_CONSTANT_SPACE);

static StructuredBuffer< float > Density = GetResource(g_DensityField.index);

[numthreads(4, 4, 4)]
void main(uint3 tID : SV_DispatchThreadID)
{
    const uint C = g_CellsPerAxis;
    if (tID.x > C || tID.y > C || tID.z > C)
        return;

    const uint dim = C + 1u + 2u * g_Apron;
    
    uint3 tx = tID + g_Apron;
    float d0 = Density[FlatTexel(tx, dim)];
    float dx = Density[FlatTexel(tx + uint3(1u, 0u, 0u), dim)];
    float dy = Density[FlatTexel(tx + uint3(0u, 1u, 0u), dim)];
    float dz = Density[FlatTexel(tx + uint3(0u, 0u, 1u), dim)];
    
    bool s0 = d0 < 0.0;
    bool sx = dx < 0.0;
    bool sy = dy < 0.0;
    bool sz = dz < 0.0;
    
    RWByteAddressBuffer Counter = GetResource(g_MCCounter.index);
    
    RWStructuredBuffer< uint > EdgeMap  = GetResource(g_EdgeMap.index);
    RWStructuredBuffer< uint > EdgeList = GetResource(g_EdgeList.index);
    
    uint keyBase = FlatTexel(tID, C + 1u) * 3u;
    
    if (tID.x < C)
    {
        uint key = keyBase + 0u;
        uint vi  = INVALID_INDEX;
        if (s0 != sx) // active edge
        {
            Counter.InterlockedAdd(8u, 1u, vi);
            if (vi < g_MaxVertices)
                EdgeList[vi] = key;
            else
                vi = INVALID_INDEX;
        }
        EdgeMap[key] = vi;
    }
    
    if (tID.y < C)
    {
        uint key = keyBase + 1u;
        uint vi  = INVALID_INDEX;
        if (s0 != sy) // active edge
        {
            Counter.InterlockedAdd(8u, 1u, vi);
            if (vi < g_MaxVertices)
                EdgeList[vi] = key;
            else
                vi = INVALID_INDEX;
        }
        EdgeMap[key] = vi;
    }
    
    if (tID.z < C)
    {
        uint key = keyBase + 2u;
        uint vi  = INVALID_INDEX;
        if (s0 != sz) // active edge
        {
            Counter.InterlockedAdd(8u, 1u, vi);
            if (vi < g_MaxVertices)
                EdgeList[vi] = key;
            else
                vi = INVALID_INDEX;
        }
        EdgeMap[key] = vi;
    }
}
