#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float g_FilterRadius; // in low-mip texels
};

ConstantBuffer< DescriptorHeapIndex > g_SrcLowTexture  : register(b1, ROOT_CONSTANT_SPACE); // previous upsample (smaller mip)
ConstantBuffer< DescriptorHeapIndex > g_SrcHighTexture : register(b2, ROOT_CONSTANT_SPACE); // downsample at target resolution
ConstantBuffer< DescriptorHeapIndex > g_OutputImage    : register(b3, ROOT_CONSTANT_SPACE);


[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Texture2D< float4 >   SrcLow      = GetResource(g_SrcLowTexture.index);
    Texture2D< float4 >   SrcHigh     = GetResource(g_SrcHighTexture.index);
    RWTexture2D< float4 > OutputImage = GetResource(g_OutputImage.index);

    int2 pixelCoord = int2(dispatchThreadID.xy);
    uint2 outSize;
    OutputImage.GetDimensions(outSize.x, outSize.y);
    if (any(pixelCoord >= outSize))
        return;

    float2 lowSize;
    SrcLow.GetDimensions(lowSize.x, lowSize.y);
    float2 texel = g_FilterRadius / lowSize;
    float2 uv    = (float2(pixelCoord) + 0.5) / float2(outSize);

    // Progressive 3x3 tent upsample
    float3 blur = 0.0;
    blur += SrcLow.SampleLevel(g_LinearClampSampler, uv + texel * float2(-1.0, -1.0), 0).rgb * 1.0;
    blur += SrcLow.SampleLevel(g_LinearClampSampler, uv + texel * float2( 0.0, -1.0), 0).rgb * 2.0;
    blur += SrcLow.SampleLevel(g_LinearClampSampler, uv + texel * float2( 1.0, -1.0), 0).rgb * 1.0;
    blur += SrcLow.SampleLevel(g_LinearClampSampler, uv + texel * float2(-1.0,  0.0), 0).rgb * 2.0;
    blur += SrcLow.SampleLevel(g_LinearClampSampler, uv,                              0).rgb * 4.0;
    blur += SrcLow.SampleLevel(g_LinearClampSampler, uv + texel * float2( 1.0,  0.0), 0).rgb * 2.0;
    blur += SrcLow.SampleLevel(g_LinearClampSampler, uv + texel * float2(-1.0,  1.0), 0).rgb * 1.0;
    blur += SrcLow.SampleLevel(g_LinearClampSampler, uv + texel * float2( 0.0,  1.0), 0).rgb * 2.0;
    blur += SrcLow.SampleLevel(g_LinearClampSampler, uv + texel * float2( 1.0,  1.0), 0).rgb * 1.0;
    blur *= (1.0 / 16.0);

    float3 high = SrcHigh.SampleLevel(g_LinearClampSampler, uv, 0).rgb;
    OutputImage[pixelCoord] = float4((high + blur) * 0.5, 1.0);
}
