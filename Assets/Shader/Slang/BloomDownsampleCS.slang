#include "ShaderShared/PostProcess.h"
#include "Common/Binding.slangh"
#include "Common/Color.slangh"

struct BloomDownsamplePush
{
    BloomDownsampleConstants constants;

    uint ID_SrcTexture;
    uint ID_OutputImage;
};

[[vk::push_constant]] ConstantBuffer< BloomDownsamplePush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Reference: Jimenez (SIGGRAPH 2014, CoD:AW)
// https://advances.realtimerendering.com/s2014/index.html
float KarisWeight(float3 c)
{
    return 1.0 / (1.0 + ConvertColorToLuminance(c));
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Texture2D< float4 >   SrcTexture  = GetResource(g_Push.ID_SrcTexture);
    RWTexture2D< float4 > OutputImage = GetResource(g_Push.ID_OutputImage);

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
    if (g_Push.constants.bFirstDownsample != 0)
    {
        float3 group0 = (a + b + d + e) * 0.25;
        float3 group1 = (b + c + e + f) * 0.25;
        float3 group2 = (d + e + g + h) * 0.25;
        float3 group3 = (e + f + h + i) * 0.25;
        float3 group4 = (j + k + l + m) * 0.25;

        float w0 = 0.125 * KarisWeight(group0);
        float w1 = 0.125 * KarisWeight(group1);
        float w2 = 0.125 * KarisWeight(group2);
        float w3 = 0.125 * KarisWeight(group3);
        float w4 = 0.5   * KarisWeight(group4);

        result = (group0 * w0 + group1 * w1 + group2 * w2 + group3 * w3 + group4 * w4) / (w0 + w1 + w2 + w3 + w4);
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
