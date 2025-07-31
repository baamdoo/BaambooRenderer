#include "Common.hlsli"

Texture2D g_SceneTexture    : register(t0);
Texture2D g_VelocityTexture : register(t1);
Texture2D g_HistoryTexture  : register(t2);

RWTexture2D< float4 > g_OutputTexture : register(u0);

SamplerState g_LinearClampSampler : register(s0);

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float blendFactor;
    uint  bFirstFrame;
};

// Convert RGB to YCoCg color space for better neighborhood clamping
float3 RGB2YCoCg(float3 rgb)
{
    float Y = dot(rgb, float3(0.25, 0.5, 0.25));
    float Co = dot(rgb, float3(0.5, 0.0, -0.5));
    float Cg = dot(rgb, float3(-0.25, 0.5, -0.25));
    return float3(Y, Co, Cg);
}

float3 YCoCg2RGB(float3 ycocg)
{
    float Y = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;
    float R = Y + Co - Cg;
    float G = Y + Cg;
    float B = Y - Co - Cg;
    return float3(R, G, B);
}

// Reference: https://www.shadertoy.com/view/MtVGWz
float4 TextureCatmullRom(Texture2D tex, SamplerState smp, float2 uv, float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5) + 0.5;

    float2 f  = samplePos - texPos1;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12      = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    float2 texPos0  = texPos1 - 1.0;
    float2 texPos3  = texPos1 + 2.0;
    float2 texPos12 = texPos1 + offset12;

    texPos0  /= texSize;
    texPos3  /= texSize;
    texPos12 /= texSize;

    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    result += tex.SampleLevel(smp, float2(texPos0.x, texPos0.y), 0) * w0.x * w0.y;
    result += tex.SampleLevel(smp, float2(texPos12.x, texPos0.y), 0) * w12.x * w0.y;
    result += tex.SampleLevel(smp, float2(texPos3.x, texPos0.y), 0) * w3.x * w0.y;
    result += tex.SampleLevel(smp, float2(texPos0.x, texPos12.y), 0) * w0.x * w12.y;
    result += tex.SampleLevel(smp, float2(texPos12.x, texPos12.y), 0) * w12.x * w12.y;
    result += tex.SampleLevel(smp, float2(texPos3.x, texPos12.y), 0) * w3.x * w12.y;
    result += tex.SampleLevel(smp, float2(texPos0.x, texPos3.y), 0) * w0.x * w3.y;
    result += tex.SampleLevel(smp, float2(texPos12.x, texPos3.y), 0) * w12.x * w3.y;
    result += tex.SampleLevel(smp, float2(texPos3.x, texPos3.y), 0) * w3.x * w3.y;

    return result;
}

// Variance clipping for better ghosting reduction
float3 ClipAABB(float3 aabbMin, float3 aabbMax, float3 history, float3 current)
{
    float3 center  = 0.5 * (aabbMax + aabbMin);
    float3 extents = 0.5 * (aabbMax - aabbMin);

    float3 v_clip = history - center;
    float3 v_unit = v_clip / extents;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return center + v_clip / ma_unit;
    else
        return history;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2  pixelCoord = int2(dispatchThreadID.xy);
    uint2 texSize;
    g_SceneTexture.GetDimensions(texSize.x, texSize.y);

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
    float3 currentYCoCg = RGB2YCoCg(currentColor);
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

    float3 clampedHistoryYCoCg = ClipAABB(minc, maxc, historyYCoCg, currentYCoCg);
    float3 clampedHistory      = YCoCg2RGB(clampedHistoryYCoCg);

    // velocity-based weight adjustment
    float velocityMagnitude = length(velocity * texSize);
    float velocityWeight    = 1.0 / (1.0 + velocityMagnitude * 0.1);

    float blendAlpha = bValidHistory ? blendFactor * velocityWeight : 1.0;

    float3 finalColor = lerp(clampedHistory, currentColor, blendAlpha);

    g_OutputTexture[pixelCoord] = float4(finalColor, 1.0);
}