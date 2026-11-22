#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/VoxelTerrainGen.slangh"

struct VoxelDensityPush
{
    VoxelTerrainGenParams constants;

    uint ID_HeightTile;
    uint ID_OutDensity;
};

[[vk::push_constant]] ConstantBuffer< VoxelDensityPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Density at a world position (SDF: solid < 0, air > 0)
float VoxelTerrainDensityFromSurface(float posY, float surfaceY)
{
    surfaceY = max(surfaceY, kVoxelWorldFloorYMeter + 0.25); // keeps the y=0 sample solid at every LOD

    float surfaceSDF = posY - surfaceY;                        // solid (<0) below the surface
    float floorSDF   = (kVoxelWorldFloorYMeter - 64.0) - posY; // one chunk below the visible world
    return max(surfaceSDF, floorSDF);                          // intersection: below surface AND above the terrain base floor
}

[numthreads(4, 4, 4)]
void main(uint3 tID : SV_DispatchThreadID)
{
    const VoxelTerrainGenParams gp = g_Push.constants;

    const uint dim = gp.samplesPerAxis + 2u * gp.apron; // C+1+2A
    if (tID.x >= dim || tID.y >= dim || tID.z >= dim)
        return;

    StructuredBuffer< float > heightTile = GetResource(g_Push.ID_HeightTile);

    float3 posWS  = VoxelTexelToWorld(gp, tID);
    float  height = heightTile[FlatTexel(tID.xz, dim)];

    RWStructuredBuffer< float > outDensity = GetResource(g_Push.ID_OutDensity);
    outDensity[FlatTexel(tID, dim)] = VoxelTerrainDensityFromSurface(posWS.y, height);
}
