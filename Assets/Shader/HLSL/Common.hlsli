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

    float3   posWORLD;
    float    zNear;
    float    zFar;
    float3   padding0;
}; ConstantBuffer< CameraData > g_Camera : register(b0);
#endif

#define ROOT_CONSTANT_SPACE space100

#endif // _HLSL_COMMON_HEADER