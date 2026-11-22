#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/VoxelMeshing.slangh"

struct VoxelEdgeVertexPush
{
    VoxelExtractConstants constants;

    uint ID_DensityField;
    uint ID_MCCounter;
    uint ID_OutVertices;
    uint ID_TriTable;
    uint ID_EdgeList;
};

[[vk::push_constant]] ConstantBuffer< VoxelEdgeVertexPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Edge list entry i becomes vertex i of the page
[numthreads(64, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    RWStructuredBuffer< uint > counter = GetResource(g_Push.ID_MCCounter);
    if (dt.x >= counter[kVoxelCounterEdgeVertex])
        return;

    const VoxelExtractConstants ex = g_Push.constants;

    const uint C   = ex.cellsPerAxis;
    const uint dim = C + 1u + 2u * ex.apron;

    StructuredBuffer< float > density  = GetResource(g_Push.ID_DensityField);
    StructuredBuffer< int >   triTable = GetResource(g_Push.ID_TriTable);
    StructuredBuffer< uint >  edgeList = GetResource(g_Push.ID_EdgeList);

    uint key  = edgeList[dt.x];
    uint axis = key % 3u;
    uint cidx = key / 3u;

    uint3 corner0 = uint3(cidx % (C + 1u), (cidx / (C + 1u)) % (C + 1u), cidx / ((C + 1u) * (C + 1u)));
    uint3 corner1 = corner0 + uint3(axis == 0u ? 1u : 0u, axis == 1u ? 1u : 0u, axis == 2u ? 1u : 0u); // max endpoint = +1 along the edge axis

    uint3 t0 = corner0 + ex.apron;
    uint3 t1 = corner1 + ex.apron;

    // sample 2-end corners along an edge + a central-difference gradient at each (apron guarantees the neighbours)
    float  cornerVal0  = density[FlatTexel(t0, dim)];
    float  cornerVal1  = density[FlatTexel(t1, dim)];
    float3 cornerPos0  = float3(corner0) * ex.voxelSizeMeter;
    float3 cornerPos1  = float3(corner1) * ex.voxelSizeMeter;
    float3 cornerGrad0 = VoxelDensityGradient(density, t0, 1u, dim);
    float3 cornerGrad1 = VoxelDensityGradient(density, t1, 1u, dim);

    RWStructuredBuffer< VoxelVertex > outVertices = GetResource(g_Push.ID_OutVertices);
    outVertices[ex.vertexPageBase + dt.x] =
        VoxelMakeEdgeVertex(ex, cornerVal0, cornerVal1, cornerPos0, cornerPos1, cornerGrad0, cornerGrad1, false, density, triTable);
}
