#include "Common.hlsli"

Texture2D g_AntiAliasedTexture : register(t0);

RWTexture2D< float4 > g_OutputImage : register(u0);

SamplerState g_LinearClampSampler : register(SAMPLER_INDEX_LINEAR_CLAMP);

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float sharpness;
};

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixelCoord  = int2(dispatchThreadID.xy);
    uint2 imageSize;
    g_OutputImage.GetDimensions(imageSize.x, imageSize.y);
    if (any(pixelCoord >= imageSize))
        return;

    float2 texelSize = 1.0 / float2(imageSize);
    float2 uv        = (float2(pixelCoord) + 0.5) * texelSize;

    // sample center and neighbors
    float3 center = g_AntiAliasedTexture.SampleLevel(g_LinearClampSampler, uv, 0).rgb;
    float3 top    = g_AntiAliasedTexture.SampleLevel(g_LinearClampSampler, uv + float2(0.0, -texelSize.y), 0).rgb;
    float3 bottom = g_AntiAliasedTexture.SampleLevel(g_LinearClampSampler, uv + float2(0.0, texelSize.y), 0).rgb;
    float3 left   = g_AntiAliasedTexture.SampleLevel(g_LinearClampSampler, uv + float2(-texelSize.x, 0.0), 0).rgb;
    float3 right  = g_AntiAliasedTexture.SampleLevel(g_LinearClampSampler, uv + float2(texelSize.x, 0.0), 0).rgb;

    // compute sharpening
    float3 sharpened = center + sharpness * (4.0 * center - top - bottom - left - right);
    float3 minColor  = min(min(min(min(center, top), bottom), left), right);
    float3 maxColor  = max(max(max(max(center, top), bottom), left), right);
    sharpened        = clamp(sharpened, minColor, maxColor);

    g_OutputImage[pixelCoord] = float4(sharpened, 1.0);
}