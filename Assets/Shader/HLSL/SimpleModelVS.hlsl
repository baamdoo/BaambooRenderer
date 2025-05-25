#include "../Common.bsh"

struct TransformData
{
    float4x4 mWorld;
    float4x4 mWorldInv;
};

StructuredBuffer< TransformData > g_Transforms : register(t0, space0);

struct CameraData
{
    float4x4 mProj;
    float4x4 mView;
    float4x4 mViewProj;
    float3   posWORLD;
    float    padding;
}; ConstantBuffer< CameraData > g_Camera : register(b1);

cbuffer DrawConstants : register(b0)
{
    uint transformID;
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
    float4 position   : SV_Position;
    float2 uv         : TEXCOORD0;
    float3 normal     : NORMAL;
    float3 tangent    : TANGENT;
};

VSOutput main(VSInput IN)
{
    VSOutput output = (VSOutput)0;

    TransformData transform = g_Transforms[transformID];

    float4 posWORLD = mul(transform.mWorld, float4(IN.position, 1.0));
    float4 normalWORLD = mul(transpose(transform.mWorldInv), float4(IN.normal, 1.0));
    float4 tangentWORLD = mul(transform.mWorld, float4(IN.tangent, 0.0));

    output.uv = IN.uv;
    output.normal = normalize(normalWORLD.xyz);
    output.tangent = tangentWORLD.xyz;
    output.position = mul(g_Camera.mViewProj, posWORLD);
    return output;
}