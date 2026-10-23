#include "Common.hlsli"
#include "VoxelTerrainCommon.hlsli"

ConstantBuffer< VoxelTerrainGenParams > g_VoxelGenParams : register(b0, space1);

ConstantBuffer< DescriptorHeapIndex > g_OutHeightRaw : register(b1, ROOT_CONSTANT_SPACE);

[numthreads(8, 8, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    VoxelTerrainGenParams gp = g_VoxelGenParams;

    const uint dim = gp.samplesPerAxis + 2u * gp.apron; // C+1+2A
    if (tID.x >= dim || tID.y >= dim)
        return;
    
    float3 posWS = VoxelTexelToWorld(gp, uint3(tID.x, 0u, tID.y));
    
    RWStructuredBuffer< float > OutHeight = GetResource(g_OutHeightRaw.index);
    OutHeight[FlatTexel(tID.xy, dim)] = VoxelTerrainSurfaceY(gp, posWS.xz);
}
