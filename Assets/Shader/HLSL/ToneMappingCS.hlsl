#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_TonemapOperator; // 0: None, 1: Reinhard, 2: ACES, 3: Uncharted2
    float g_EV100;
    float g_Gamma;
};

ConstantBuffer< DescriptorHeapIndex > g_SceneTexture : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutputImage  : register(b2, ROOT_CONSTANT_SPACE);


float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

float3 ACESFilm(float3 x)
{
    const float3x3 ACESInputMat =
    {
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    };

    const float3x3 ACESOutputMat =
    {
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    };

    x = mul(ACESInputMat, x);
    x = RRTAndODTFit(x);
    x = mul(ACESOutputMat, x);
    return saturate(x);
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
    RWTexture2D< float4 > OutputImage  = GetResource(g_OutputImage.index);
    Texture2D< float4 >   SceneTexture = GetResource(g_SceneTexture.index);

    int2 pixelCoord = int2(dispatchThreadID.xy);
    uint2 imageSize;
    OutputImage.GetDimensions(imageSize.x, imageSize.y);

    if (any(pixelCoord >= imageSize))
        return;

    float2 uv       = (float2(pixelCoord) + 0.5) / float2(imageSize);
    float3 hdrColor = SceneTexture.SampleLevel(g_LinearClampSampler, uv, 0).rgb;

    // exposure correction
    float ev100    = g_EV100;
    float exposure = 1.0 / pow(2.0, ev100);
    hdrColor *= exposure;

    float3 toneMapped;
    switch (g_TonemapOperator)
    {
    case 0: // None
        toneMapped = hdrColor;
        break;

    case 1: // Reinhard
        toneMapped = hdrColor / (1.0 + hdrColor);
        break;

    case 2: // ACES
        toneMapped = ACESFilm(hdrColor);
        break;

    case 3: // Uncharted2
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

    OutputImage[pixelCoord] = float4(gammaCorrected, 1.0);
}
