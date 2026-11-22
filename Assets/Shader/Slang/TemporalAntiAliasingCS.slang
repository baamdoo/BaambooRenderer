#include "ShaderShared/PostProcess.h"
#include "Common/Binding.slangh"
#include "Common/Color.slangh"

struct TemporalAntiAliasingPush
{
    TemporalAntiAliasingConstants constants;

    uint ID_SceneTexture;
    uint ID_VelocityTexture;
    uint ID_HistoryTexture;
    uint ID_OutputImage;
};

[[vk::push_constant]] ConstantBuffer< TemporalAntiAliasingPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Karis (2014): resolve in tonemapped space so one ultra-bright sample (physical sky is
// ~1e3-1e4 cd/m2 against ~1e2 surfaces) cannot dominate edge averages and re-harden
// silhouettes after display mapping. History stays linear; weight on fetch, invert on store.
float3 KarisTonemap(float3 c)
{
    return c / (1.0 + ConvertColorToLuminance(c));
}

float3 KarisInverseTonemap(float3 c)
{
    return c / max(1.0 - ConvertColorToLuminance(c), 1e-6);
}

float3 RGB2YCoCg(float3 rgb)
{
    float Y  = dot(rgb, float3( 0.25, 0.5,  0.25));
    float Co = dot(rgb, float3( 0.5,  0.0, -0.5));
    float Cg = dot(rgb, float3(-0.25, 0.5, -0.25));
    return float3(Y, Co, Cg);
}

float3 YCoCg2RGB(float3 ycocg)
{
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;

    float R = Y + Co - Cg;
    float G = Y + Cg;
    float B = Y - Co - Cg;

    return float3(R, G, B);
}

// Reference: https://www.shadertoy.com/view/MtVGWz
// The 9 taps need bilinear filtering: each middle tap blends two texels by w2 / (w1 + w2).
float4 TextureCatmullRom(Texture2D< float4 > tex, float2 uv, float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1   = floor(samplePos - 0.5) + 0.5;

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

    float4 result = float4(0.0);
    result += tex.SampleLevel(g_LinearClampSampler, float2(texPos0.x,  texPos0.y),  0) * w0.x  * w0.y;
    result += tex.SampleLevel(g_LinearClampSampler, float2(texPos12.x, texPos0.y),  0) * w12.x * w0.y;
    result += tex.SampleLevel(g_LinearClampSampler, float2(texPos3.x,  texPos0.y),  0) * w3.x  * w0.y;
    result += tex.SampleLevel(g_LinearClampSampler, float2(texPos0.x,  texPos12.y), 0) * w0.x  * w12.y;
    result += tex.SampleLevel(g_LinearClampSampler, float2(texPos12.x, texPos12.y), 0) * w12.x * w12.y;
    result += tex.SampleLevel(g_LinearClampSampler, float2(texPos3.x,  texPos12.y), 0) * w3.x  * w12.y;
    result += tex.SampleLevel(g_LinearClampSampler, float2(texPos0.x,  texPos3.y),  0) * w0.x  * w3.y;
    result += tex.SampleLevel(g_LinearClampSampler, float2(texPos12.x, texPos3.y),  0) * w12.x * w3.y;
    result += tex.SampleLevel(g_LinearClampSampler, float2(texPos3.x,  texPos3.y),  0) * w3.x  * w3.y;

    return result;
}

float3 ClipAABB(float3 aabbMin, float3 aabbMax, float3 history)
{
    float3 center  = 0.5 * (aabbMax + aabbMin);
    // A flat neighborhood has zero extent, and 0 / 0 would make the history NaN.
    float3 extents = max(0.5 * (aabbMax - aabbMin), float3(1e-6));

    float3 v_clip  = history - center;
    float3 v_unit  = v_clip / extents;
    float3 a_unit  = abs(v_unit);
    float  ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return center + v_clip / ma_unit;
    else
        return history;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    RWTexture2D< float4 > OutputImage = GetResource(g_Push.ID_OutputImage);

    int2  pixelCoord = int2(dispatchThreadID.xy);
    uint2 imageSize;
    OutputImage.GetDimensions(imageSize.x, imageSize.y);
    if (any(pixelCoord >= imageSize))
        return;

    float2 texSize = float2(imageSize);
    float2 uv      = (float2(pixelCoord) + 0.5) / texSize;

    Texture2D< float4 > SceneTexture    = GetResource(g_Push.ID_SceneTexture);
    Texture2D< float4 > VelocityTexture = GetResource(g_Push.ID_VelocityTexture);
    Texture2D< float4 > HistoryTexture  = GetResource(g_Push.ID_HistoryTexture);

    // velocity = currUV - prevUV in texture UV (y down)
    float2 velocity      = VelocityTexture.SampleLevel(g_LinearClampSampler, uv, 0).xy;
    float2 historyUV     = uv - velocity;
    bool   bValidHistory = all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0));

    float3 currentColor = KarisTonemap(SceneTexture.SampleLevel(g_LinearClampSampler, uv, 0).rgb);
    float3 historyColor = currentColor;
    if (bValidHistory && g_Push.constants.bFirstFrame == 0)
    {
        // max(0): Catmull-Rom negative lobes can undershoot on HDR history
        historyColor = KarisTonemap(max(TextureCatmullRom(HistoryTexture, historyUV, texSize).rgb, float3(0.0)));
    }

    // neighborhood statistics in YCoCg space
    float3 m1 = float3(0.0);
    float3 m2 = float3(0.0);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float2 sampleUV      = uv + float2(float(x), float(y)) / texSize;
            float3 neighborColor = KarisTonemap(SceneTexture.SampleLevel(g_LinearClampSampler, sampleUV, 0).rgb);
            float3 neighborYCoCg = RGB2YCoCg(neighborColor);

            m1 += neighborYCoCg;
            m2 += neighborYCoCg * neighborYCoCg;
        }
    }

    // Variance Clipping
    const float gamma = 1.0; // tightness of the clipping
    float3 mu    = m1 / 9.0;
    float3 sigma = sqrt(abs(m2 / 9.0 - mu * mu));
    float3 minc  = mu - gamma * sigma;
    float3 maxc  = mu + gamma * sigma;

    float3 clippedHistoryYCoCg = ClipAABB(minc, maxc, RGB2YCoCg(historyColor));
    // Rounding can leave tiny negatives here, and the tone-mapping pow turns them into NaN.
    float3 clippedHistory      = max(YCoCg2RGB(clippedHistoryYCoCg), float3(0.0));

    // velocity-based weight adjustment
    const float kMotionBlendAlpha = 0.2;
    const float kMotionFullPixels = 40.0;
    const float blendFactor       = g_Push.constants.blendFactor;

    float velocityPixels = length(velocity * texSize);
    float motionAlpha    = lerp(blendFactor, kMotionBlendAlpha, saturate(velocityPixels / kMotionFullPixels));
    float blendAlpha     = bValidHistory ? max(blendFactor, motionAlpha) : 1.0;

    float3 finalColor = KarisInverseTonemap(lerp(clippedHistory, currentColor, blendAlpha));

    OutputImage[pixelCoord] = float4(finalColor, 1.0);
}
