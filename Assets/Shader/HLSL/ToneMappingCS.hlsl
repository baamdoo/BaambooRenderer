#include "Common.hlsli"

Texture2D g_SceneTexture : register(t0);

RWTexture2D< float4 > g_OutputImage : register(u0);

SamplerState g_LinearClampSampler : register(SAMPLER_INDEX_LINEAR_CLAMP);

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_TonemapOperator; // 0: Reinhard, 1: ACES, 2: Uncharted2
    float g_EV100;
    float g_Gamma;
};

float3 ACESFilm(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float3 Uncharted2Tonemap(float3 x)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixelCoord = int2(dispatchThreadID.xy);
    uint2 imageSize;
    g_OutputImage.GetDimensions(imageSize.x, imageSize.y);

    if (any(pixelCoord >= imageSize))
        return;

    float2 uv       = (float2(pixelCoord) + 0.5) / float2(imageSize);
    float3 hdrColor = g_SceneTexture.SampleLevel(g_LinearClampSampler, uv, 0).rgb;

    // exposure correction
    float ev100    = g_EV100;
    float exposure = 1.0 / pow(2.0, ev100);
    hdrColor *= exposure;

    float3 toneMapped;
    switch (g_TonemapOperator)
    {
    case 0: // Reinhard
        toneMapped = hdrColor / (1.0 + hdrColor);
        break;

    case 1: // ACES
        toneMapped = ACESFilm(hdrColor);
        break;

    case 2: // Uncharted2
    {
        const float W = 11.2;
        float3 curr       = Uncharted2Tonemap(hdrColor);
        float3 whiteScale = 1.0 / Uncharted2Tonemap(float3(W, W, W));

        toneMapped = curr * whiteScale;
        break;
    }

    default:
        toneMapped = hdrColor;
        break;
    }

    float3 gammaCorrected = pow(toneMapped, float3(1.0 / g_Gamma, 1.0 / g_Gamma, 1.0 / g_Gamma));

    g_OutputImage[pixelCoord] = float4(gammaCorrected, 1.0);
}