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
ConstantBuffer< DescriptorHeapIndex > g_OutVertices  : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TriTable     : register(b4, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_EdgeList     : register(b5, ROOT_CONSTANT_SPACE);

static StructuredBuffer<float> Density  = GetResource(g_DensityField.index);
static StructuredBuffer<int>   TriTable = GetResource(g_TriTable.index);

[numthreads(64, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    RWByteAddressBuffer Counter = GetResource(g_MCCounter.index);
    if (dt.x >= Counter.Load(16u)) // E = list entries that own a vertex slot
        return;
    
    const uint C   = g_CellsPerAxis;
    const uint dim = C + 1u + 2u * g_Apron;
    
    StructuredBuffer< uint > EdgeList = GetResource(g_EdgeList.index);
    uint key  = EdgeList[dt.x];
    uint axis = key % 3;
    uint cidx = key / 3;
    
    uint3 corner0 = uint3(cidx % (C + 1), (cidx / (C + 1)) % (C + 1), cidx / ((C + 1) * (C + 1)));
    uint3 corner1 = corner0 + uint3(axis == 0u, axis == 1u, axis == 2u); // max endpoint = +1 along the edge axis
    
    uint3 t0 = corner0 + g_Apron;
    uint3 t1 = corner1 + g_Apron;
    
    // sample 2-end corners along an edge + a central-difference gradient at each (apron guarantees the neighbours)
    float  cornerVal0  = Density[FlatTexel(t0, dim)];
    float  cornerVal1  = Density[FlatTexel(t1, dim)];
    float3 cornerPos0  = float3(corner0) * g_VoxelSizeMeter;
    float3 cornerPos1  = float3(corner1) * g_VoxelSizeMeter;
    float3 cornerGrad0 = float3(
            Density[FlatTexel(t0 + uint3(1u, 0u, 0u), dim)] - Density[FlatTexel(t0 - uint3(1u, 0u, 0u), dim)],
            Density[FlatTexel(t0 + uint3(0u, 1u, 0u), dim)] - Density[FlatTexel(t0 - uint3(0u, 1u, 0u), dim)],
            Density[FlatTexel(t0 + uint3(0u, 0u, 1u), dim)] - Density[FlatTexel(t0 - uint3(0u, 0u, 1u), dim)]);
    float3 cornerGrad1 = float3(
            Density[FlatTexel(t1 + uint3(1u, 0u, 0u), dim)] - Density[FlatTexel(t1 - uint3(1u, 0u, 0u), dim)],
            Density[FlatTexel(t1 + uint3(0u, 1u, 0u), dim)] - Density[FlatTexel(t1 - uint3(0u, 1u, 0u), dim)],
            Density[FlatTexel(t1 + uint3(0u, 0u, 1u), dim)] - Density[FlatTexel(t1 - uint3(0u, 0u, 1u), dim)]);
    
    VoxelTerrainGenParams gp = (VoxelTerrainGenParams)0;
    gp.cellsPerAxis   = C;
    gp.apron          = g_Apron;
    gp.voxelSizeMeter = g_VoxelSizeMeter;
    
    RWStructuredBuffer< VoxelVertex > OutV = GetResource(g_OutVertices.index);
    OutV[g_VertexSlabBase + dt.x] = 
        VoxelMakeEdgeVertex(gp, cornerVal0, cornerVal1, cornerPos0, cornerPos1, cornerGrad0, cornerGrad1, g_GeomorphRadiusMeter, float(C) * g_VoxelSizeMeter, false, Density, TriTable);
}
