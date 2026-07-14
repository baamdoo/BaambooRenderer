#include "Common.hlsli"
#include "VoxelTerrainCommon.hlsli"

ConstantBuffer< VoxelTerrainGenParams > g_VoxelGenParams : register(b0, space1);

ConstantBuffer< DescriptorHeapIndex > g_OutErosionMap : register(b1, ROOT_CONSTANT_SPACE);

[numthreads(8, 8, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2D< float4 > OutMap = GetResource(g_OutErosionMap.index);

    uint mapW, mapH;
    OutMap.GetDimensions(mapW, mapH);
    if (tID.x >= mapW || tID.y >= mapH)
        return;

    VoxelTerrainGenParams gp = g_VoxelGenParams;

    float  chunkSize = float(gp.cellsPerAxis) * gp.voxelSizeMeter;
    float2 uv = (float2(tID.xy) + 0.5) / float(mapW);
    float2 xz = float2(gp.originX, gp.originZ) + uv * chunkSize;

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

        float geoMinWL = VoxelErosionGeoMinWavelength(gp);     // geometry band
        float outMinWL = 4.0 * chunkSize / float(mapW);        // bake texel Nyquist (2x margin)
        d = VoxelErosionFilterEx(gp, xz, has, fadeTarget, geoMinWL, min(outMinWL, geoMinWL), ridge, geoDelta);
    }

    // detail = full-band minus geometry-band height, both measured on the clamped surface
    float  h01Geo       = saturate(hs.x + geoDelta.x / amp);
    float  h01Full      = saturate(hs.x + d.x / amp);
    float  detailHeight = (h01Full - h01Geo) * amp; // height delta (m) beyond the geometry band
    float  surfaceY     = gp.originY + gp.surfaceLevelRatio * chunkSize + (h01Geo - 0.5) * gp.mountainAmplitude;

    OutMap[tID.xy] = float4(detailHeight, ridge, surfaceY, 0.0);
}
