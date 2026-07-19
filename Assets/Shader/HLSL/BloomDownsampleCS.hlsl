#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_bFirstDownsample;
};

ConstantBuffer< DescriptorHeapIndex > g_SrcTexture  : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutputImage : register(b2, ROOT_CONSTANT_SPACE);


// Reference: Jimenez (SIGGRAPH 2014, CoD:AW)
// https://advances.realtimerendering.com/s2014/index.html
float KarisWeight(float3 c)
{
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Texture2D< float4 >   SrcTexture  = GetResource(g_SrcTexture.index);
    RWTexture2D< float4 > OutputImage = GetResource(g_OutputImage.index);

    int2 pixelCoord = int2(dispatchThreadID.xy);
    uint2 outSize;
    OutputImage.GetDimensions(outSize.x, outSize.y);
    if (any(pixelCoord >= outSize))
        return;

    float2 srcSize;
    SrcTexture.GetDimensions(srcSize.x, srcSize.y);
    float2 texel = 1.0 / srcSize;
    float2 uv    = (float2(pixelCoord) + 0.5) / float2(outSize);

    float3 a = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2(-2.0, -2.0), 0).rgb;
    float3 b = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2( 0.0, -2.0), 0).rgb;
    float3 c = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2( 2.0, -2.0), 0).rgb;
    float3 d = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2(-2.0,  0.0), 0).rgb;
    float3 e = SrcTexture.SampleLevel(g_LinearClampSampler, uv,                              0).rgb;
    float3 f = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2( 2.0,  0.0), 0).rgb;
    float3 g = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2(-2.0,  2.0), 0).rgb;
    float3 h = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2( 0.0,  2.0), 0).rgb;
    float3 i = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2( 2.0,  2.0), 0).rgb;
    float3 j = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2(-1.0, -1.0), 0).rgb;
    float3 k = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2( 1.0, -1.0), 0).rgb;
    float3 l = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2(-1.0,  1.0), 0).rgb;
    float3 m = SrcTexture.SampleLevel(g_LinearClampSampler, uv + texel * float2( 1.0,  1.0), 0).rgb;

    float3 result;
    if (g_bFirstDownsample != 0)
    {
        float3 group0 = (a + b + d + e) * 0.25;
        float3 group1 = (b + c + e + f) * 0.25;
        float3 group2 = (d + e + g + h) * 0.25;
        float3 group3 = (e + f + h + i) * 0.25;
        float3 group4 = (j + k + l + m) * 0.25;

        result = group0 * (0.125 * KarisWeight(group0))
               + group1 * (0.125 * KarisWeight(group1))
               + group2 * (0.125 * KarisWeight(group2))
               + group3 * (0.125 * KarisWeight(group3))
               + group4 * (0.5   * KarisWeight(group4));
        result /= (0.125 * (KarisWeight(group0) + KarisWeight(group1) + KarisWeight(group2) + KarisWeight(group3)) + 0.5 * KarisWeight(group4));
    }
    else
    {
        result = e * 0.125
               + (a + c + g + i) * 0.03125
               + (b + d + f + h) * 0.0625
               + (j + k + l + m) * 0.125;
    }

    OutputImage[pixelCoord] = float4(result, 1.0);
}
