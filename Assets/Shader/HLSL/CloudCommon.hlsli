#ifndef _HLSL_CLOUD_COMMON_HEADER
#define _HLSL_CLOUD_COMMON_HEADER

#define _HLSL
#include "../Common.bsh"
#include "HelperFunctions.hlsli"

#ifdef _CLOUD
struct CloudData
{
    float  topLayer_km;
    float  bottomLayer_km;
    float2 padding0;

    float3 extinctionStrength;
    float  extinctionScale;
    
    float msContribution;
    float msOcclusion;
    float msEccentricity;
    float groundContributionStrength;

    float coverage;
    float cloudType;
    float baseNoiseScale;
    float baseIntensity;

    float erosionNoiseScale;
    float erosionIntensity;
    float erosionPower;
    float wispiness;
    float billowiness;
    float precipitation;
    float erosionHeightGradientMultiplier;
    float erosionHeightGradientPower;

    float3 windDirection;
    float  windSpeed_mps;
};
ConstantBuffer< CloudData > g_Cloud : register(b2);

Texture3D< float4 > g_CloudBaseNoise    : register(t0);
Texture3D< float4 > g_CloudErosionNoise : register(t1);
Texture2D< float >  g_TopGradientLUT    : register(t2);
Texture2D< float >  g_BottomGradientLUT : register(t3);

SamplerState g_LinearClampSampler : register(SAMPLER_INDEX_LINEAR_CLAMP);
SamplerState g_LinearWrapSampler  : register(SAMPLER_INDEX_LINEAR_WRAP);
SamplerState g_PointClampSampler  : register(SAMPLER_INDEX_POINT_CLAMP);

////////////////////////////////////////////////////////////////////////
// Shaping //
// Reference: https://advances.realtimerendering.com/s2022/index.html#Nubis
float GetCloudBaseShape(float3 pos, float hNorm)
{
    float3 baseUVW   = pos * g_Cloud.baseNoiseScale;
    float4 baseNoise = g_CloudBaseNoise.Sample(g_LinearWrapSampler, baseUVW);
    
    float billowy   = dot(baseNoise.gba, float3(0.625, 0.125, 0.25));
    float baseCloud = billowy - (1.0 - baseNoise.r);

    return baseCloud * g_Cloud.baseIntensity;
}

float GetErosion(float3 pos)
{
    // TODO. Dissipation using CurlNoise
    float3 erosionUVW   = pos * g_Cloud.erosionNoiseScale;
    float4 erosionNoise = g_CloudErosionNoise.Sample(g_LinearWrapSampler, erosionUVW);

    float wispy   = lerp(erosionNoise.r, erosionNoise.g, g_Cloud.wispiness);
    float billowy = lerp(erosionNoise.b, erosionNoise.a, g_Cloud.billowiness);
    float erosion = lerp(wispy, billowy, g_Cloud.precipitation);
          erosion = pow(erosion, 1.0 - g_Cloud.erosionPower);

    return erosion * g_Cloud.erosionIntensity;
}

float SampleCloudDensity(float3 pos, float hNorm, float3 offset)
{
    if (hNorm < 0.0)
    {
        return 0.0;
    }

    pos -= offset;

    float2 topGradientUV = float2(g_Cloud.cloudType, 1.0 - hNorm);
    float  topGradient   = g_TopGradientLUT.Sample(g_LinearWrapSampler, topGradientUV).r;

    float2 bottomGradientUV = float2(g_Cloud.cloudType, 1.0 - hNorm);
    float  bottomGradient   = g_BottomGradientLUT.Sample(g_LinearWrapSampler, topGradientUV).r;

    float heightFactor = (hNorm * (1.0 - hNorm));

    float densityGradient = topGradient * bottomGradient;
          densityGradient *= heightFactor;

    // Make the middle part is most affected
    float heightGradient  = (1.0 - 1.7 * heightFactor) * (1.0 - g_Cloud.coverage);

    float b = GetCloudBaseShape(pos, hNorm);
          b = saturate(b - heightGradient);
          b *= densityGradient * g_Cloud.baseIntensity;

    if (b > 0.0)
    {
        float erosionHeightGradient = pow(saturate(hNorm * g_Cloud.erosionHeightGradientMultiplier), g_Cloud.erosionHeightGradientPower);

        float e = GetErosion(pos) * erosionHeightGradient;

        b -= e;
    }

    return saturate(b);
}

#endif // _CLOUD

#endif // _HLSL_CLOUD_COMMON_HEADER