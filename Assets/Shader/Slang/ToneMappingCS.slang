#include "ShaderShared/PostProcess.h"
#include "Common/Binding.slangh"

struct ToneMappingPush
{
    ToneMappingConstants constants;

    uint ID_SceneTexture; 
    uint ID_OutputImage; 
    uint ID_BloomTexture;
};

[[vk::push_constant]] ConstantBuffer< ToneMappingPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

//////////////////////////////////////////////////////////////////////////
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
         1.60475,  -0.53108, -0.07367,
        -0.10208, 1.10813, -0.00605,
        -0.00327, -0.07276, 1.07602
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

// Uchimura 2017, "HDR theory and practice"
// https://www.slideshare.net/nikuque/hdr-theory-and-practicce-jp
float3 UchimuraTonemap(float3 x)
{
    const float P = 1.0;  // max display brightness
    const float a = 1.0;  // contrast
    const float m = 0.22; // linear section start
    const float l = 0.4;  // linear section length
    const float c = 1.33; // black tightness
    const float b = 0.0;  // pedestal

    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    float3 w0 = 1.0 - smoothstep(0.0, m, x);
    float3 w2 = step(m + l0, x);
    float3 w1 = 1.0 - w0 - w2;

    float3 T = m * pow(x / m, c) + b;
    float3 S = P - (P - S1) * exp(CP * (x - S0));
    float3 L = m + a * (x - m);

    return saturate(T * w0 + L * w1 + S * w2);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    RWTexture2D< float4 > OutputImage = GetResource(g_Push.ID_OutputImage);
    Texture2D< float4 > SceneTexture  = GetResource(g_Push.ID_SceneTexture);

    int2 pixelCoord = int2(dispatchThreadID.xy);

    uint2 imageSize;
    OutputImage.GetDimensions(imageSize.x, imageSize.y);

    if (any(pixelCoord >= imageSize))
        return;

    float2 uv = (float2(pixelCoord) + 0.5) / float2(imageSize);
    float3 hdrColor = SceneTexture.SampleLevel(g_LinearClampSampler, uv, 0).rgb;

    // energy-conserving bloom mix, in HDR before exposure
    if (g_Push.constants.bloomStrength > 0.0)
    {
        Texture2D< float4 > BloomTexture = GetResource(g_Push.ID_BloomTexture);
        float3 bloom = BloomTexture.SampleLevel(g_LinearClampSampler, uv, 0).rgb;
        hdrColor = lerp(hdrColor, bloom, g_Push.constants.bloomStrength);
    }

    // exposure correction
    float ev100 = g_Push.constants.ev100;
    float exposure = 1.0 / pow(2.0, ev100);
    hdrColor *= exposure;

    float3 toneMapped;
    switch (g_Push.constants.tonemapOp)
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
        float3 curr = Uncharted2Tonemap(hdrColor);
        float3 whiteScale = 1.0 / Uncharted2Tonemap(float3(W, W, W));

        toneMapped = curr * whiteScale;
        break;
    }

    case 4: // Uchimura
        toneMapped = UchimuraTonemap(hdrColor);
        break;

    default:
        toneMapped = hdrColor;
        break;
    }

    float3 gammaCorrected = pow(toneMapped, float3(1.0 / g_Push.constants.gamma, 1.0 / g_Push.constants.gamma, 1.0 / g_Push.constants.gamma));

    OutputImage[pixelCoord] = float4(gammaCorrected, 1.0);
}
