#include "Common.hlsli"
#include "VoxelTerrainCommon.hlsli"

ConstantBuffer< VoxelTerrainGenParams > g_VoxelGenParams : register(b0, space1);

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_ErosionSlice; // erosion map array slice of the chunk being baked
};

ConstantBuffer< DescriptorHeapIndex > g_OutErosionMap : register(b1, ROOT_CONSTANT_SPACE);

[numthreads(8, 8, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2DArray< float4 > OutMap = GetResource(g_OutErosionMap.index);

    uint mapW, mapH, mapSlices;
    OutMap.GetDimensions(mapW, mapH, mapSlices);
    if (tID.x >= mapW || tID.y >= mapH)
        return;

    VoxelTerrainGenParams gp = g_VoxelGenParams;

    float3 originWS  = VoxelChunkOriginWS(gp);
    float  chunkSize = float(gp.cellsPerAxis) * gp.voxelSizeMeter;
    float  inner     = float(mapW) - 2.0 * float(VOXEL_EROSION_APRON);
    float2 xz = originWS.xz + ((float2(tID.xy) + 0.5 - float(VOXEL_EROSION_APRON)) / inner) * chunkSize;

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

    OutMap[uint3(tID.xy, g_ErosionSlice)] = float4(detailHeight, ridge, surfaceY, 0.0);
}
