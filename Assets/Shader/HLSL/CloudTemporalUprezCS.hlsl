#define _CAMERA
#include "Common.hlsli"
#include "HelperFunctions.hlsli"

Texture2D< float4 > g_CloudScatteringLUT             : register(t0);
Texture2D< float4 > g_PrevUprezzedCloudScatteringLUT : register(t1);
Texture2D< float >  g_DepthBuffer                    : register(t2);

RWTexture2D< float4 > g_UprezzedCloudScatteringLUT : register(u0);

SamplerState g_LinearClampSampler : register(SAMPLER_INDEX_LINEAR_CLAMP);
SamplerState g_PointClampSampler  : register(SAMPLER_INDEX_POINT_CLAMP);

cbuffer Push : register(b0, ROOT_CONSTANT_SPACE)
{
    float  g_BlendFactor;
    float2 g_InvLowResTexSize;
};

float4 VarianceColorClampAABB(float4 historyColor, Texture2D cloudLUT, float2 uv, float2 invLowResTexSize)
{
    float3 historyYCoCg = RGB2YCoCg(historyColor.rgb);

    float3 m1 = float3(0.0, 0.0, 0.0);
    float3 m2 = float3(0.0, 0.0, 0.0);
    float mAlpha1 = 0.0;
    float mAlpha2 = 0.0;

    // sample a 3x3 neighborhood around the current pixel
    [loop]
    for (float x = -1.0; x <= 1.0; x += 1.0)
    {
        [loop]
        for (float y = -1.0; y <= 1.0; y += 1.0)
        {
            float2 sampleUV = uv + float2(x, y) * invLowResTexSize;

            // Common.hg에 g_SamplerLinear가 정의되어 있다고 가정합니다.
            float4 neighborColor = cloudLUT.Sample(g_LinearClampSampler, sampleUV);

            float3 neighborYCoCg = RGB2YCoCg(neighborColor.rgb);
            m1 += neighborYCoCg;
            m2 += neighborYCoCg * neighborYCoCg;

            float neighborAlpha = neighborColor.a;
            mAlpha1 += neighborAlpha;
            mAlpha2 += neighborAlpha * neighborAlpha;
        }
    }

    // Variance Clipping : https://developer.download.nvidia.com/gameworks/events/GDC2016/msalvi_temporal_supersampling.pdf
    const float gamma = 5.0; // tightness of the clipping
    float3 mu    = m1 / 9.0;
    float3 sigma = sqrt(abs(m2 / 9.0 - mu * mu));
    float3 minc  = mu - gamma * sigma;
    float3 maxc  = mu + gamma * sigma;

    float3 clampedHistoryYCoCg = ClipAABB(minc, maxc, historyYCoCg);
    float3 clampedHistoryRGB   = YCoCg2RGB(clampedHistoryYCoCg);

    //
    const float gammaAlpha = 1.0;
    float muAlpha    = mAlpha1 / 9.0;
    float sigmaAlpha = sqrt(abs(mAlpha2 / 9.0 - muAlpha * muAlpha));
    float mincAlpha  = muAlpha - gammaAlpha * sigmaAlpha;
    float maxcAlpha  = muAlpha + gammaAlpha * sigmaAlpha;

    float clampedHistoryAlpha = ClipAABB(mincAlpha, maxcAlpha, historyColor.a);

    return float4(clampedHistoryRGB, clampedHistoryAlpha);
}

[numthreads(8, 8, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    uint2 imgSize;
    uint2 pixCoords = tID.xy;
    g_UprezzedCloudScatteringLUT.GetDimensions(imgSize.x, imgSize.y);
    if (tID.x >= imgSize.x || tID.y >= imgSize.y)
        return;

    float2 uv       = (float2(pixCoords) + 0.5) / float2(imgSize);

    float4 currentColor = g_CloudScatteringLUT.Sample(g_LinearClampSampler, uv);
    float  depth        = g_DepthBuffer.Sample(g_PointClampSampler, uv).r;

    float3 posWORLD = ReconstructWorldPos(uv, depth, g_Camera.mViewProjInv);

    float4 posPrevClip = mul(g_Camera.mViewProjUnjitteredPrev, float4(posWORLD, 1.0));
    float3 posPrevNDC  = posPrevClip.xyz / posPrevClip.w;
    float2 prevUV      = posPrevNDC.xy * 0.5 + 0.5;
           prevUV.y    = 1.0 - prevUV.y;

    float4 newColor = currentColor;
    if (all(prevUV > float2(0.0, 0.0)) && all(prevUV < float2(1.0, 1.0)))
    {
        float4 historyColor = g_PrevUprezzedCloudScatteringLUT.Sample(g_LinearClampSampler, prevUV);
        // historyColor = VarianceColorClampAABB(historyColor, g_CloudScatteringLUT, uv, g_InvLowResTexSize);

        newColor = lerp(historyColor, currentColor, g_BlendFactor);
    }

    g_UprezzedCloudScatteringLUT[pixCoords] = newColor;
}