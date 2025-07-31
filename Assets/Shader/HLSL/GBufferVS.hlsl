#define _CAMERA
#include "Common.hlsli"

StructuredBuffer< TransformData > g_Transforms : register(t0, space0);

cbuffer RootConstants : register(b1, space0)
{
    uint g_TransformIndex;
};

struct VSInput 
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD;
    float3 normal   : NORMAL;
    float3 tangent  : TANGENT;
};

struct VSOutput
{
    float4 position     : SV_Position;
    float4 posCurrCLIP  : POSITION0;
    float4 posPrevCLIP  : POSITION1;
    float3 posWORLD     : POSITION2;
    float2 uv           : TEXCOORD0;
    float3 normalWORLD  : TEXCOORD1;
    float3 tangentWORLD : TEXCOORD2;
};

VSOutput main(VSInput IN)
{
    VSOutput output = (VSOutput)0;

    TransformData transform = g_Transforms[g_TransformIndex];

    float4 posWORLD     = mul(transform.mWorldToView, float4(IN.position, 1.0));
    float4 normalWORLD  = mul(transform.mWorldToView, float4(IN.normal, 0.0));
    float4 tangentWORLD = mul(transform.mWorldToView, float4(IN.tangent, 0.0));

    output.posWORLD     = posWORLD.xyz;
    output.uv           = IN.uv;
    output.normalWORLD  = normalize(normalWORLD.xyz);
    output.tangentWORLD = normalize(tangentWORLD.xyz);
    output.position     = mul(g_Camera.mViewProj, posWORLD);

    // TODO
    output.posPrevCLIP = mul(g_Camera.mViewProjUnjitteredPrev, posWORLD);
    output.posCurrCLIP = mul(g_Camera.mViewProjUnjittered, posWORLD);
    return output;
}