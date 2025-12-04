#ifndef _HLSL_COMMON_HEADER
#define _HLSL_COMMON_HEADER

#define _HLSL
#include "../Common.bsh"

struct DescriptorHeapIndex
{
    uint index;
};

#ifdef _CAMERA
struct CameraData
{
    // Camera-relative matrices
    float4x4 mView;
    float4x4 mProj;
    float4x4 mViewProj;
    float4x4 mViewProjInv;
    float4x4 mViewProjUnjittered;
	float4x4 mViewProjUnjitteredPrev;

    float3   posWORLD;
    float    zNear;
    float    zFar;
    float3   padding0;
}; ConstantBuffer< CameraData > g_Camera : register(b0, space0);
#endif // _CAMERA

#ifdef _TRANSFORM
ConstantBuffer< DescriptorHeapIndex > g_Transforms : register(b1, space0);
#endif // _TRANSFORM

#ifdef _MATERIAL
ConstantBuffer< DescriptorHeapIndex > g_Materials : register(b2, space0);
#endif // _MATERIAL

#ifdef _LIGHT
ConstantBuffer< LightingData > g_Lights : register(b3, space0);
#endif // _LIGHT

struct AtmosphereData
{
    DirectionalLight light;

    float  planetRadiusKm;
    float  atmosphereRadiusKm;
    float2 padding0;

    float3 rayleighScattering;
    float  rayleighDensityKm;
    
    float mieScattering;
    float mieAbsorption;
    float mieDensityKm;
    float miePhaseG;
    
    float3 ozoneAbsorption;
    float  ozoneCenterKm;
    float3 groundAlbedo;
    float  ozoneWidthKm;
};

struct CloudData
{
    float  topLayerKm;
    float  bottomLayerKm;
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
    float  windSpeedMps;
};
#ifdef _SCENEENVIRONMENT
struct SceneEnvironmentData
{
    AtmosphereData atmosphere;
	CloudData      cloud;
};
ConstantBuffer< SceneEnvironmentData > g_SceneEnvironment : register(b4, space0);

AtmosphereData GetAtmosphereData()
{
	return g_SceneEnvironment.atmosphere;
}

CloudData GetCloudData()
{
	return g_SceneEnvironment.cloud;
}
#endif // _SCENEENVIRONMENT

#define GetResource(idx) ResourceDescriptorHeap[NonUniformResourceIndex(idx)]

#define ROOT_CONSTANT_SPACE space100

#define SAMPLER_INDEX_LINEAR_CLAMP     s0
#define SAMPLER_INDEX_LINEAR_WRAP      s1
#define SAMPLER_INDEX_POINT_CLAMP      s2
#define SAMPLER_INDEX_POINT_WRAP       s3
#define SAMPLER_INDEX_TILINEAR_WRAP    s4
#define SAMPLER_INDEX_ANISOTROPIC_WRAP s5
#define SAMPLER_INDEX_CMP_LESS_EQUAL   s6

SamplerState g_LinearClampSampler      : register(SAMPLER_INDEX_LINEAR_CLAMP);
SamplerState g_LinearWrapSampler       : register(SAMPLER_INDEX_LINEAR_WRAP);
SamplerState g_PointClampSampler       : register(SAMPLER_INDEX_POINT_CLAMP);
SamplerState g_PointWrapSampler        : register(SAMPLER_INDEX_POINT_WRAP);
SamplerState g_TrilinearWrapSampler    : register(SAMPLER_INDEX_TILINEAR_WRAP);
SamplerState g_AnisotropicWrapSampler  : register(SAMPLER_INDEX_ANISOTROPIC_WRAP);
SamplerState g_CompareLessEqualSampler : register(SAMPLER_INDEX_CMP_LESS_EQUAL);

#endif // _HLSL_COMMON_HEADER