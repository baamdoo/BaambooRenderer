#include "Common.hlsli"
#include "HelperFunctions.hlsli"
#include "Noise.hlsli"

RWTexture3D< float4 > g_DetailNoise : register(u0);

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    // r-channel
    float rWeight;
    float rFrequency;
    uint  rOctaves;
    float rPersistence;
    float rLacunarity;

    // g-channel
    float gWeight;
    float gFrequency;
    uint  gOctaves;
    float gPersistence;
    float gLacunarity;

    // b-channel
    float bWeight;
    float bFrequency;
    uint  bOctaves;
    float bPersistence;
    float bLacunarity;
};

[numthreads(8, 8, 8)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height, depth;
    g_DetailNoise.GetDimensions(width, height, depth);
    int3 imgSize = int3(width, height, depth);

    int3 pixCoords = (int3)dispatchThreadID.xyz;

    if (any(pixCoords >= imgSize))
        return;

    float3 uvw = (float3(pixCoords) + 0.5) / (float3)imgSize;

    float r = worleyFBM(uvw, 8.0);
    float g = worleyFBM(uvw, 16.0);
    float b = worleyFBM(uvw, 32.0);
    float a = max(0.0, 1.0 - (r * rWeight + g * gWeight + b * bWeight) / 1.75);

    float4 value = float4(r, g, b, a);
    g_DetailNoise[pixCoords] = value;
}