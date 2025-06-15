#ifndef _HLSL_COMMON_HEADER
#define _HLSL_COMMON_HEADER
#include "../Common.bsh"

#ifdef _CAMERA
struct CameraData
{
    float4x4 mProj;
    float4x4 mView;
    float4x4 mViewProj;
    float3   posWORLD;
    float    padding;
}; ConstantBuffer< CameraData > g_Camera : register(b0);
#endif

#endif // _HLSL_COMMON_HEADER