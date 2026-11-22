#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/VoxelTerrainGen.slangh"

struct VoxelHeightRawPush
{
    VoxelTerrainGenParams constants;

    uint ID_OutHeightRaw;
};

[[vk::push_constant]] ConstantBuffer< VoxelHeightRawPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Geometry-band wrapper: evaluates down to the geometry wavelength only
float4 VoxelErosionFilter(VoxelTerrainGenParams gp, float2 p, float3 heightAndSlope, float fadeTarget, out float ridgeMap)
{
    float3 geoUnused;
    return VoxelErosionFilterEx(gp, p, heightAndSlope, fadeTarget, gp.geoMinWavelengthMeter, gp.geoMinWavelengthMeter, ridgeMap, geoUnused);
}

// Base height + erosion delta
float VoxelTerrainErodedHeight01(VoxelTerrainGenParams gp, float2 xz)
{
    float3 heightSlope = VoxelTerrainHeight01Deriv(gp, xz);
    if (gp.erosionOctaves == 0u)
        return heightSlope.x;

    // fade target: valley -1 .. peak +1
    float fadeTarget = clamp((heightSlope.x - 0.5) * 2.0 / 0.6, -1.0, 1.0);

    // filter slope: coarse FD of the final height
    float  amp = max(gp.mountainAmplitude, 1e-4);
    float2 g   = VoxelTerrainCoarseGrad(gp, xz, 0.25 * gp.erosionScale * gp.erosionCellScale);
    float3 hs  = float3(heightSlope.x * amp, g * amp * gp.erosionSlopeScale);

    float  ridge;
    float4 d = VoxelErosionFilter(gp, xz, hs, fadeTarget, ridge);

    return saturate(heightSlope.x + d.x / amp); // delta meters -> h01
}

float VoxelTerrainSurfaceY(VoxelTerrainGenParams gp, float2 xzWS)
{
    float h01 = VoxelTerrainErodedHeight01(gp, xzWS);
    return gp.surfaceBaseYMeter + (h01 - 0.5) * gp.mountainAmplitude;
}

[numthreads(8, 8, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    const VoxelTerrainGenParams gp = g_Push.constants;

    const uint dim = gp.samplesPerAxis + 2u * gp.apron; // C+1+2A
    if (tID.x >= dim || tID.y >= dim)
        return;

    float3 posWS = VoxelTexelToWorld(gp, uint3(tID.x, 0u, tID.y));

    RWStructuredBuffer< float > outHeight = GetResource(g_Push.ID_OutHeightRaw);
    outHeight[FlatTexel(tID.xy, dim)] = VoxelTerrainSurfaceY(gp, posWS.xz);
}
