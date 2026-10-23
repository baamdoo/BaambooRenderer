#include "Common.hlsli"
#include "VoxelTerrainCommon.hlsli"

ConstantBuffer< VoxelTerrainGenParams > g_VoxelGenParams : register(b0, space1);

ConstantBuffer< DescriptorHeapIndex > g_OutDensity : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_HeightTile : register(b2, ROOT_CONSTANT_SPACE);

[numthreads(4, 4, 4)]
void main(uint3 tID : SV_DispatchThreadID)
{
    VoxelTerrainGenParams gp = g_VoxelGenParams;

    const uint dim = gp.samplesPerAxis + 2u * gp.apron; // C+1+2A
    if (tID.x >= dim || tID.y >= dim || tID.z >= dim)
        return;

    StructuredBuffer< float > HeightTile = GetResource(g_HeightTile.index);
    
    float3 posWS  = VoxelTexelToWorld(gp, tID);
    float height = HeightTile[FlatTexel(tID.xz, dim)];
    
    RWStructuredBuffer< float > OutDensity = GetResource(g_OutDensity.index);
    OutDensity[FlatTexel(tID, dim)] = VoxelTerrainDensityFromSurface(posWS.y, height);
}
