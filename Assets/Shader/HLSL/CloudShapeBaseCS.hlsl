#include "Common.hlsli"
#include "NoiseCommon.hlsli"
#include "HelperFunctions.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_OutBaseNoise : register(b1, ROOT_CONSTANT_SPACE);

[numthreads(8, 8, 8)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    RWTexture3D< float4 > OutBaseNoise = GetResource(g_OutBaseNoise.index);

    uint width, height, depth;
    OutBaseNoise.GetDimensions(width, height, depth);
    int3 imgSize = int3(width, height, depth);

    int3 pixCoords = (int3)dispatchThreadID.xyz;
    if (any(pixCoords >= imgSize))
        return;

    float3 uvw     = (float3(pixCoords) + 0.5) / (float3)imgSize;

    /*float perlin1 = perlinFBM(uvw, 10.0, 6, 0.5, 2.0, 0x2u) * 0.5 + 0.5;
    float perlin2 = perlinFBM(uvw, 4.0, 4, 0.2, 2.0, 0x1u) * 0.5 + 0.5;
    float worley0      = worleyFBM(uvw, 4.0);
    float worley1      = worleyFBM(uvw, 8.0);
    float worley2      = worleyFBM(uvw, 16.0);
    float worley3      = worleyFBM(uvw, 32.0);
    float worley4      = worleyFBM(uvw, 64.0);
    float perlinWorley1 = remap(perlin1, 1.0 - worley1, 1.0, 0.0, 1.0);
    float perlinWorley2 = lerp(perlin1, worley3, 0.3);
    float perlinWorley3 = perlin1 * worley3;
    float perlinWorley4 = lerp(perlin1, worley3, 0.5);
    float4 value = float4(perlinWorley1, perlinWorley2, perlinWorley3, perlinWorley4);*/

    float alligator0 = alligatorFBM(uvw, 2.0, 4, 0.5, 2.0, 0x1511u);
    float alligator1 = alligatorFBM(uvw, 3.0, 6, 0.5, 2.0, 0x0u);

    float4 value = float4(smoothstep(0.3, 0.7, alligator0), smoothstep(0.3, 0.8, alligator1), pow(1.0 - alligator0, 2.0), (pow(1.0 - alligator1, 2.0) + 1.0) * 0.5);
    OutBaseNoise[pixCoords] = value;
}