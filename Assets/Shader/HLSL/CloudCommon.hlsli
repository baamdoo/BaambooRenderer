#ifndef _HLSL_CLOUD_COMMON_HEADER
#define _HLSL_CLOUD_COMMON_HEADER

#define _HLSL
#include "Common.hlsli"
#include "HelperFunctions.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_CloudBaseNoise    : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CloudErosionNoise : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TopGradientLUT    : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_BottomGradientLUT : register(b4, ROOT_CONSTANT_SPACE);


////////////////////////////////////////////////////////////////////////
// Shaping //
// Reference: https://advances.realtimerendering.com/s2022/index.html#Nubis
float GetCloudBaseShape(float3 pos, float hNorm, inout CloudData Cloud)
{
    Texture3D< float4 > CloudBaseNoise = GetResource(g_CloudBaseNoise.index);

    float3 baseUVW   = pos * Cloud.baseNoiseScale;
    float4 baseNoise = CloudBaseNoise.Sample(g_LinearWrapSampler, baseUVW);
    
    float billowy   = dot(baseNoise.gba, float3(0.625, 0.125, 0.25));
    float baseCloud = billowy - (1.0 - baseNoise.r);

    return baseCloud * Cloud.baseIntensity;
}

float GetErosion(float3 pos, inout CloudData Cloud)
{
    Texture3D< float4 > CloudErosionNoise = GetResource(g_CloudErosionNoise.index);

    // TODO. Dissipation using CurlNoise
    float3 erosionUVW   = pos * Cloud.erosionNoiseScale;
    float4 erosionNoise = CloudErosionNoise.Sample(g_LinearWrapSampler, erosionUVW);

    float wispy   = lerp(erosionNoise.r, erosionNoise.g, Cloud.wispiness);
    float billowy = lerp(erosionNoise.b, erosionNoise.a, Cloud.billowiness);
    float erosion = lerp(wispy, billowy, Cloud.precipitation);
          erosion = pow(erosion, 1.0 - Cloud.erosionPower);

    return erosion * Cloud.erosionIntensity;
}

float SampleCloudDensity(float3 pos, float hNorm, float3 offset, inout CloudData Cloud)
{
    Texture2D< float > TopGradientLUT    = GetResource(g_TopGradientLUT.index);
    Texture2D< float > BottomGradientLUT = GetResource(g_BottomGradientLUT.index);


    if (hNorm < 0.0)
    {
        return 0.0;
    }

    pos -= offset;

    float2 topGradientUV = float2(Cloud.cloudType, 1.0 - hNorm);
    float  topGradient   = TopGradientLUT.Sample(g_LinearWrapSampler, topGradientUV).r;

    float2 bottomGradientUV = float2(Cloud.cloudType, 1.0 - hNorm);
    float  bottomGradient   = BottomGradientLUT.Sample(g_LinearWrapSampler, topGradientUV).r;

    float heightFactor = (hNorm * (1.0 - hNorm));

    float densityGradient = topGradient * bottomGradient;
          densityGradient *= heightFactor;

    // Make the middle part is most affected
    float heightGradient = (1.0 - 1.7 * heightFactor) * (1.0 - Cloud.coverage);

    float b = GetCloudBaseShape(pos, hNorm, Cloud);
          b = saturate(b - heightGradient);
	      b *= densityGradient * Cloud.baseIntensity;

    if (b > 0.0)
    {
        float erosionHeightGradient = pow(saturate(hNorm * Cloud.erosionHeightGradientMultiplier), Cloud.erosionHeightGradientPower);

        float e = GetErosion(pos, Cloud) * erosionHeightGradient;

        b -= e;
    }

    return saturate(b);
}

#endif // _HLSL_CLOUD_COMMON_HEADER