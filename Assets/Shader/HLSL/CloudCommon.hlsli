#ifndef _HLSL_CLOUD_COMMON_HEADER
#define _HLSL_CLOUD_COMMON_HEADER

#define _HLSL
#include "Common.hlsli"
#include "HelperFunctions.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_CloudMacroMap   : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CloudBaseNoise  : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CloudProfileLUT : register(b3, ROOT_CONSTANT_SPACE);


////////////////////////////////////////////////////////////////////////
// Shaping //
// Reference: https://www.guerrilla-games.com/read/nubis-cubed
float GetCloudFloorVariationLevel(float4 cloudMap, float hNorm, float macroDensity, inout CloudData Cloud)
{
    float cloudsVariation = lerp(cloudMap.r, cloudMap.a, 0.6);

    float varAlpha       = saturate(macroDensity - 1.5);
    float floorVariation = lerp(Cloud.floorVariationClear, Cloud.floorVariationCloudy, varAlpha);
    floorVariation *= -1.0 * pow(cloudMap.r, 2.0);

    return saturate(lerp(saturate(hNorm), 1.0, floorVariation));
}

float3 SampleCloudConservativeDensity(float3 pos, float hNorm, inout CloudData Cloud)
{
    Texture2D< float4 > CloudMacroMap   = GetResource(g_CloudMacroMap.index);
    Texture2D< float3 > CloudProfileLUT = GetResource(g_CloudProfileLUT.index);
    
    float2 cloudUV  = pos.zx / Cloud.cloudsScale;
    float4 cloudMap = CloudMacroMap.Sample(g_LinearWrapSampler, cloudUV);

    //--- Macro Cloud Map ---//
    float2 variation = 0.5 - 0.5 * cos(cloudUV * 2.0 * PI);

    float clumpsVariation = variation.x * variation.y * Cloud.clumpsVariation;
    float macroDensity    = clumpsVariation + Cloud.baseDensity;

    float densityThreshold = pow(1.0 - (macroDensity / 6.5), 3.0) - 0.25;

    //--- Profile ---//
    float floorVariationLevel = GetCloudFloorVariationLevel(cloudMap, hNorm, macroDensity, Cloud);

    float2 profileUV = float2(macroDensity / 3.0, 1.0 - floorVariationLevel);
    float  profileR  = CloudProfileLUT.Sample(g_LinearWrapSampler, profileUV).r;

    //--- Cloud Shape ---//
    float cloudShape = saturate(cloudMap.r * cloudMap.g - densityThreshold) / (1.0 - densityThreshold);
    float dimensionalProfile = saturate(cloudShape - (1.0 - profileR));

    return float3(dimensionalProfile, floorVariationLevel, macroDensity);
}

struct CloudDensityResult
{
    float uprezzedDensity;
    float splitAltitude;
};
CloudDensityResult VolumetricCloudsExtinction(float3 pos, float hNorm, float distToCamera, float3 conservativeDensity, bool bOptimize, inout CloudData Cloud)
{
    Texture3D< float4 > CloudLowrezNoise = GetResource(g_CloudBaseNoise.index);
    Texture2D< float3 > CloudProfileLUT  = GetResource(g_CloudProfileLUT.index);
    
    //--- Profile ---//
    float floorVariationLevel = conservativeDensity.g;
    float splitAltitude       = floorVariationLevel;

    float u = conservativeDensity.b / 3.0;
    float v = 1.0 - floorVariationLevel;

    float2 profileUV     = float2(u, v);
    float3 profileSample = CloudProfileLUT.Sample(g_LinearWrapSampler, profileUV);

    float profileTextureG = profileSample.g;
    float profileTextureB = profileSample.b;

    //--- Base Erosion ---//
    float macroDensity = conservativeDensity.b;

    float3 cloudUVW     = pos / Cloud.baseErosionScale;
    float4 lowFreqNoise = CloudLowrezNoise.Sample(g_LinearWrapSampler, cloudUVW);

    float erosionFactor = max(profileTextureB, 0.3) * Cloud.baseErosionStrength;
    float wispy   = lerp(lowFreqNoise.r, lowFreqNoise.g, conservativeDensity.r);
    float billowy = lerp(lowFreqNoise.b, lowFreqNoise.a, pow(conservativeDensity.r, 0.25));

    float noiseBlendAlpha = pow(conservativeDensity.r, 0.25);
    float lowFreqErosion  = erosionFactor * pow(lerp(wispy, billowy, noiseBlendAlpha), Cloud.baseErosionPower);

    float cloudDensity = saturate(remap(conservativeDensity.r, lowFreqErosion, 1.0, 0.0, 1.0));
    if (bOptimize)
    {
        CloudDensityResult result;
        result.uprezzedDensity = cloudDensity;
        result.splitAltitude   = splitAltitude;

        return result;
    }

    //--- High Erosion ---//
    // float distRatio  = (Cloud.hfOctaveZeroDistance * 0.6) / max(distToCamera, 0.001);
    // float lodFromFov = distRatio / max(Cloud.cameraFOV, 0.001);

    float heightDensityMod = pow(profileTextureG, 3.0) * Cloud.extinctionScale;

    float highFreqNoise = saturate(
        lerp(1.0 - pow(abs(abs(lowFreqNoise.g * 2.0 - 1.0) * 2.0 - 1.0), 4.0 * Cloud.hfErosionDistortion),
            pow(abs(abs(lowFreqNoise.b * 2.0 - 1.0) * 2.0 - 1.0), 2.0 * Cloud.hfErosionDistortion), noiseBlendAlpha));
    float noiseComposite = lerp(highFreqNoise, lowFreqErosion, Cloud.hfErosionStrength);

    float uprezzedDensity = saturate(safeRemap(conservativeDensity.r, noiseComposite, 1.0, 0.0, 1.0)) * heightDensityMod;
    //uprezzedDensity = pow(uprezzedDensity, lerp(0.3, 0.6, max(EPSILON_MIN, heightDensityMod)));
    //uprezzedDensity *= lowFreqNoise.b;
    
    CloudDensityResult result;
    result.uprezzedDensity = max(uprezzedDensity, 0.0);
    result.splitAltitude   = splitAltitude;

    return result;
}

struct CloudExtinctionResult
{
    float extinction;
    float emissiveLerpAlpha;
};
CloudExtinctionResult SampleCloudExtinction(float3 pos, float hNorm, float distToCamera, float3 conservativeDensity, bool bOptimize, inout CloudData Cloud)
{
    CloudDensityResult density = VolumetricCloudsExtinction(pos, hNorm, distToCamera, conservativeDensity, bOptimize, Cloud);

    //float fadeAlpha = (distToCamera - Cloud.closeFadeOffset) / Cloud.closeFadeDistance;
    //      fadeAlpha = saturate(fadeAlpha);

    float invAltitude       = 1.0 - density.splitAltitude;
    float emissiveLerpAlpha = pow(invAltitude, 0.03);

    CloudExtinctionResult result;
    result.extinction        = density.uprezzedDensity;
    result.emissiveLerpAlpha = emissiveLerpAlpha;

    return result;
}

#endif // _HLSL_CLOUD_COMMON_HEADER