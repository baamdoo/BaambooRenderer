#include "Common.hlsli"
#include "HelperFunctions.hlsli"
#include "Noise.hlsli"

RWTexture3D< float4 > g_BaseNoise : register(u0);

[numthreads(8, 8, 8)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height, depth;
    g_BaseNoise.GetDimensions(width, height, depth);
    int3 imgSize = int3(width, height, depth);

    int3 pixCoords = (int3)dispatchThreadID.xyz;
    if (any(pixCoords >= imgSize))
        return;

    float3 uvw = (float3(pixCoords) + 0.5) / (float3)imgSize;

    // float noise = remap(perlinNoise3D(uvw * 10.0), -1.0, 1.0, 0.0, 1.0);
    // float noise = ridgedFBM(uvw * 10.0, 4, 0.5, 2.0);
    // float noise = turbulenceFBM(uvw * 10.0, 4, 0.5, 2.0);
    // float noise = worleyNoise3D(uvw, 10.0);
    // float noise = 1.0 - worleyNoise3D(uvw, 10.0);
    // float noise = perlinWorley3D(uvw, 10.0);
    // noise = steppedNoise(noise);

    // uint seed = 1u;
    float perlin = perlinFBM(uvw, 8.0, 7, exp(-0.85), 2.0);
    perlin = lerp(1.0, perlin, 0.5);
    perlin = abs(perlin * 2.0 - 1.0);
    //perlin = (perlin + 1.0) * 0.5;
    float worley0 = worleyFBM(uvw, 6.0);
    float worley1 = worleyFBM(uvw, 8.0);
    float worley2 = worleyFBM(uvw, 12.0);
    float perlinWorley = remap(perlin, 0.0, 1.0, worley0, 1.0);

    float4 value = float4(perlinWorley, worley0, worley1, worley2);
    g_BaseNoise[pixCoords] = value;
}