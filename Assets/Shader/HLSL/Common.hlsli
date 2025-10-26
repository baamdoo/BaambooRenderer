#ifndef _HLSL_COMMON_HEADER
#define _HLSL_COMMON_HEADER

#define _HLSL
#include "../Common.bsh"

#ifdef _CAMERA
struct CameraData
{
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
}; ConstantBuffer< CameraData > g_Camera : register(b0);
#endif

#define ROOT_CONSTANT_SPACE space100

#define SAMPLER_INDEX_LINEAR_CLAMP     s0
#define SAMPLER_INDEX_LINEAR_WRAP      s1
#define SAMPLER_INDEX_POINT_CLAMP      s2
#define SAMPLER_INDEX_POINT_WRAP       s3
#define SAMPLER_INDEX_TILINEAR_WRAP    s4
#define SAMPLER_INDEX_ANISOTROPIC_WRAP s5
#define SAMPLER_INDEX_CMP_LESS_EQUAL   s6

#endif // _HLSL_COMMON_HEADER