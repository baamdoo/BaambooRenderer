#define _HLSL
#define _CAMERA
#include "Common.hlsli"

StructuredBuffer< TransformData > g_Transforms : register(t0, space0);

cbuffer RootConstants : register(b1, space0)
{
    uint TransformIndex;
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
    float4 posCLIP      : SV_Position;
    float3 posWORLD     : POSITION;
    float2 uv           : TEXCOORD0;
    float3 normalWORLD  : TEXCOORD1;
    float3 tangentWORLD : TEXCOORD2;
};

VSOutput main(VSInput IN, uint vid : SV_VertexID)
{
    VSOutput output = (VSOutput)0;

    TransformData transform = g_Transforms[TransformIndex];

    float4 posWORLD     = mul(transform.mWorldToView, float4(IN.position, 1.0));
    float4 normalWORLD  = mul(transpose(transform.mViewToWorld), float4(IN.normal, 1.0));
    float4 tangentWORLD = mul(transform.mWorldToView, float4(IN.tangent, 0.0));

    output.posWORLD     = posWORLD.xyz;
    output.uv           = IN.uv;
    output.normalWORLD  = normalize(normalWORLD.xyz);
    output.tangentWORLD = tangentWORLD.xyz;
    output.posCLIP      = mul(g_Camera.mViewProj, posWORLD);
    return output;
}