#define _CAMERA
#include "AtmosphereCommon.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_SkyViewLUT   : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutSkyboxLUT : register(b2, ROOT_CONSTANT_SPACE);


static const float2 atanInv = float2(0.1591, 0.3183);

float3 GetRayDirectionFromCubemapCoord(uint3 tID, uint width, uint height)
{
    float2 uv  = (float2(tID.xy) + 0.5f) / float2(width, height);
    float2 ndc = uv * 2.0f - 1.0f;

    float3 rayDir = float3(0, 0, 0);

    switch (tID.z)
    {
    case 0: rayDir = float3(1.0, -ndc.y, -ndc.x);  break; // +X
    case 1: rayDir = float3(-1.0, -ndc.y, ndc.x);  break; // -X
    case 2: rayDir = float3(ndc.x, 1.0, ndc.y);    break; // +Y
    case 3: rayDir = float3(ndc.x, -1.0, -ndc.y);  break; // -Y
    case 4: rayDir = float3(ndc.x, -ndc.y, 1.0);   break; // +Z
    case 5: rayDir = float3(-ndc.x, -ndc.y, -1.0); break; // -Z
    }

    return normalize(rayDir);
}

float2 GetEquirectangularUV(float3 rayDir)
{
    float2 uv = float2(atan2Fast(rayDir.z, rayDir.x), asin(rayDir.y));
    uv *= atanInv;
    uv += 0.5f;

    uv.y = 1.0f - uv.y;
    return uv;
}

[numthreads(8, 8, 6)]
void main(uint3 pixCoords : SV_DispatchThreadID)
{
    Texture2D< float4 >        SkyViewLUT   = GetResource(g_SkyViewLUT.index);
    RWTexture2DArray< float3 > OutSkyboxLUT = GetResource(g_OutSkyboxLUT.index);

    uint width, height, elements;
    OutSkyboxLUT.GetDimensions(width, height, elements);

    if (pixCoords.x >= width || pixCoords.y >= height)
        return;

    float3 rayDir = GetRayDirectionFromCubemapCoord(pixCoords, width, height);
    float2 skyUV  = GetEquirectangularUV(normalize(rayDir));

    float4 skyColor = SkyViewLUT.SampleLevel(g_LinearClampSampler, skyUV, 0);
    float3 color    = skyColor.rgb;

    OutSkyboxLUT[pixCoords] = color;
}