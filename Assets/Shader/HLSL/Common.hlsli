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
    float    padding;
}; ConstantBuffer< CameraData > g_Camera : register(b0);
#endif

float3 ReconstructWorldPos(float2 uv, float depth, float4x4 mViewProjInv)
{
    float4 clipPos  = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 worldPos = mul(mViewProjInv, clipPos);

    return worldPos.xyz / worldPos.w;
}

static const float MIN_ROUGHNESS = 0.045;

#endif // _HLSL_COMMON_HEADER