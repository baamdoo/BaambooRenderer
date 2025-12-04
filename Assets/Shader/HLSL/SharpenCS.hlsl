#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float sharpness;
};

ConstantBuffer< DescriptorHeapIndex > g_AntiAliasedTexture : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutputImage        : register(b2, ROOT_CONSTANT_SPACE);


[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    RWTexture2D< float4 > OutputImage = GetResource(g_OutputImage.index);

    int2 pixelCoord  = int2(dispatchThreadID.xy);
    uint2 imageSize;
    OutputImage.GetDimensions(imageSize.x, imageSize.y);
    if (any(pixelCoord >= imageSize))
        return;

    float2 texelSize = 1.0 / float2(imageSize);
    float2 uv        = (float2(pixelCoord) + 0.5) * texelSize;

    Texture2D< float4 > AntiAliasedTexture = GetResource(g_AntiAliasedTexture.index);

    // sample center and neighbors
    float3 center = AntiAliasedTexture.SampleLevel(g_LinearClampSampler, uv, 0).rgb;
    float3 top    = AntiAliasedTexture.SampleLevel(g_LinearClampSampler, uv + float2(0.0, -texelSize.y), 0).rgb;
    float3 bottom = AntiAliasedTexture.SampleLevel(g_LinearClampSampler, uv + float2(0.0, texelSize.y), 0).rgb;
    float3 left   = AntiAliasedTexture.SampleLevel(g_LinearClampSampler, uv + float2(-texelSize.x, 0.0), 0).rgb;
    float3 right  = AntiAliasedTexture.SampleLevel(g_LinearClampSampler, uv + float2(texelSize.x, 0.0), 0).rgb;

    // compute sharpening
    float3 sharpened = center + sharpness * (4.0 * center - top - bottom - left - right);
    float3 minColor  = min(min(min(min(center, top), bottom), left), right);
    float3 maxColor  = max(max(max(max(center, top), bottom), left), right);
    sharpened        = clamp(sharpened, minColor, maxColor);

    OutputImage[pixelCoord] = float4(sharpened, 1.0);
}