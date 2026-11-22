#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/VoxelTerrainGen.slangh"

struct VoxelErosionBakePush
{
    VoxelErosionBakeConstants constants;

    uint ID_OutErosionMap;
};

[[vk::push_constant]] ConstantBuffer< VoxelErosionBakePush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

float3 VoxelChunkOriginWS(VoxelTerrainGenParams gp)
{
    return float3(int3(gp.chunkCoordX, gp.chunkCoordY, gp.chunkCoordZ) * int(gp.cellsPerAxis)) * gp.voxelSizeMeter;
}

[numthreads(8, 8, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2DArray< float4 > outMap = GetResource(g_Push.ID_OutErosionMap);

    uint mapW, mapH, mapSlices;
    outMap.GetDimensions(mapW, mapH, mapSlices);
    if (tID.x >= mapW || tID.y >= mapH)
        return;

    const VoxelTerrainGenParams gp = g_Push.constants.genParams;

    float3 originWS  = VoxelChunkOriginWS(gp);
    float  chunkSize = float(gp.cellsPerAxis) * gp.voxelSizeMeter;
    float  inner     = float(mapW) - 2.0 * float(kVoxelErosionApron);
    float2 xz = originWS.xz + ((float2(tID.xy) + 0.5 - float(kVoxelErosionApron)) / inner) * chunkSize;

    float3 hs  = VoxelTerrainHeight01Deriv(gp, xz);
    float  amp = max(gp.mountainAmplitude, 1e-4);

    float4 d        = float4(0.0, 0.0, 0.0, 0.0);
    float3 geoDelta = float3(0.0, 0.0, 0.0);
    float  ridge    = 0.0;
    if (gp.erosionOctaves != 0u)
    {
        float  fadeTarget = clamp((hs.x - 0.5) * 2.0 / 0.6, -1.0, 1.0);
        float2 g   = VoxelTerrainCoarseGrad(gp, xz, 0.25 * gp.erosionScale * gp.erosionCellScale);
        float3 has = float3(hs.x * amp, g * amp * gp.erosionSlopeScale);

        float geoMinWavelength = gp.geoMinWavelengthMeter; // geometry band
        float outMinWavelength = 4.0 * chunkSize / inner;  // bake texel Nyquist (2x margin)
        d = VoxelErosionFilterEx(gp, xz, has, fadeTarget, geoMinWavelength, min(outMinWavelength, geoMinWavelength), ridge, geoDelta);
    }

    // detail = full-band minus geometry-band height, both measured on the clamped surface
    float h01Geo       = saturate(hs.x + geoDelta.x / amp);
    float h01Full      = saturate(hs.x + d.x / amp);
    float detailHeight = (h01Full - h01Geo) * amp; // height delta (m) beyond the geometry band
    float surfaceY     = gp.surfaceBaseYMeter + (h01Geo - 0.5) * gp.mountainAmplitude;

    outMap[uint3(tID.xy, g_Push.constants.erosionSlice)] = float4(detailHeight, ridge, surfaceY, 0.0);
}
