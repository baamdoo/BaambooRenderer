#include "Common.hlsli"
#include "VoxelTerrainCommon.hlsli"

ConstantBuffer< VoxelTerrainGenParams > g_VoxelGenParams : register(b0, space1);

ConstantBuffer< DescriptorHeapIndex > g_OutDensityTex   : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutDensityDebug : register(b2, ROOT_CONSTANT_SPACE);

[numthreads(4, 4, 4)]
void main(uint3 tID : SV_DispatchThreadID)
{
    VoxelTerrainGenParams gp = g_VoxelGenParams;

    const uint dim = gp.samplesPerAxis + 2u * gp.apron; // C+1+2A
    if (tID.x >= dim || tID.y >= dim || tID.z >= dim)
        return;

    float3 worldPos = VoxelTexelToWorld(gp, tID);
    float  density  = VoxelTerrainDensity(gp, worldPos);

    RWTexture3D< float >        OutDensity = GetResource(g_OutDensityTex.index);
    RWStructuredBuffer< float > OutDebug   = GetResource(g_OutDensityDebug.index);

    OutDensity[tID] = density;

    uint flatIndex = (tID.z * dim + tID.y) * dim + tID.x;
    OutDebug[flatIndex] = density;
}
