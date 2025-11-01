#include "Common.hlsli"
#include "HelperFunctions.hlsli"

Texture2D g_SceneTexture    : register(t0);
Texture2D g_VelocityTexture : register(t1);
Texture2D g_HistoryTexture  : register(t2);

RWTexture2D< float4 > g_OutputImage : register(u0);

SamplerState g_LinearClampSampler : register(SAMPLER_INDEX_LINEAR_CLAMP);

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float blendFactor;
    uint  bFirstFrame;
};

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2  pixelCoord = int2(dispatchThreadID.xy);
    uint2 texSize;
    g_OutputImage.GetDimensions(texSize.x, texSize.y);

    if (any(pixelCoord >= texSize))
        return;

    float2 uv        = (float2(pixelCoord) + 0.5) / float2(texSize);

    float2 velocity      = g_VelocityTexture.SampleLevel(g_LinearClampSampler, uv, 0).xy;
    float2 historyUV     = uv - velocity;
    bool   bValidHistory = all(historyUV >= float2(0.0, 0.0)) && all(historyUV <= float2(1.0, 1.0));

    float3 currentColor = g_SceneTexture.SampleLevel(g_LinearClampSampler, uv, 0).rgb;
    float3 historyColor = float3(0.0, 0.0, 0.0);
    if (bValidHistory && bFirstFrame == 0)
    {
        historyColor = TextureCatmullRom(g_HistoryTexture, g_LinearClampSampler, historyUV, texSize).rgb;
    }
    else
    {
        historyColor = currentColor;
    }

    // neighborhood clamping in YCoCg space
    float3 historyYCoCg = RGB2YCoCg(historyColor);

    float3 m1 = float3(0.0, 0.0, 0.0);
    float3 m2 = float3(0.0, 0.0, 0.0);
    float3 neighborMin = float3(1e10, 1e10, 1e10);
    float3 neighborMax = float3(-1e10, -1e10, -1e10);

    // sample a 3x3 neighborhood around the current pixel
    for (float x = -1.0; x <= 1.0; x += 1.0)
    {
        for (float y = -1.0; y <= 1.0; y += 1.0)
        {
            float2 sampleUV      = uv + float2(x, y) / texSize;
            float3 neighborColor = g_SceneTexture.SampleLevel(g_LinearClampSampler, sampleUV, 0).rgb;
            float3 neighborYCoCg = RGB2YCoCg(neighborColor);

            m1 += neighborYCoCg;
            m2 += neighborYCoCg * neighborYCoCg;
            neighborMin = min(neighborMin, neighborYCoCg);
            neighborMax = max(neighborMax, neighborYCoCg);
        }
    }

    // Variance Clipping
    const float gamma = 1.0; // tightness of the clipping
    float3 mu    = m1 / 9.0;
    float3 sigma = sqrt(abs(m2 / 9.0 - mu * mu));
    float3 minc  = mu - gamma * sigma;
    float3 maxc  = mu + gamma * sigma;

    float3 clampedHistoryYCoCg = ClipAABB(minc, maxc, historyYCoCg);
    float3 clampedHistory      = YCoCg2RGB(clampedHistoryYCoCg);

    // velocity-based weight adjustment
    float velocityMagnitude = length(velocity * texSize);
    float velocityWeight    = 1.0 / (1.0 + velocityMagnitude * 0.1);

    float blendAlpha = bValidHistory ? blendFactor * velocityWeight : 1.0;

    float3 finalColor = lerp(clampedHistory, currentColor, blendAlpha);

    g_OutputImage[pixelCoord] = float4(finalColor, 1.0);
}