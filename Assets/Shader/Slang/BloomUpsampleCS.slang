#include "ShaderShared/PostProcess.h"
#include "Common/Binding.slangh"

struct BloomUpsamplePush
{
    BloomUpsampleConstants constants;

    uint ID_SrcLowTexture;  // previous upsample (smaller mip)
    uint ID_SrcHighTexture; // downsample at target resolution
    uint ID_OutputImage;
};

[[vk::push_constant]] ConstantBuffer< BloomUpsamplePush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Texture2D< float4 >   SrcLow      = GetResource(g_Push.ID_SrcLowTexture);
    Texture2D< float4 >   SrcHigh     = GetResource(g_Push.ID_SrcHighTexture);
    RWTexture2D< float4 > OutputImage = GetResource(g_Push.ID_OutputImage);

    int2 pixelCoord = int2(dispatchThreadID.xy);
    uint2 outSize;
    OutputImage.GetDimensions(outSize.x, outSize.y);
    if (any(pixelCoord >= outSize))
        return;

    float2 lowSize;
    SrcLow.GetDimensions(lowSize.x, lowSize.y);
    float2 texel = g_Push.constants.filterRadius / lowSize;
    float2 uv    = (float2(pixelCoord) + 0.5) / float2(outSize);

    // Progressive 3x3 tent upsample
    float3 blur = float3(0.0);
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
