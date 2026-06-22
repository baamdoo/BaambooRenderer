#define _CAMERA
#define _TRANSFORM
#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_TransformIndex;
    uint g_MaterialIndex;
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

    StructuredBuffer< TransformData > Transforms = GetResource(g_Transforms.index);

    TransformData transform = Transforms[g_TransformIndex];

    float4 posWORLD = mul(transform.mLocalToWorld, float4(IN.position, 1.0));

    float3 normalWORLD  = normalize(mul(transpose((float3x3)transform.mWorldToLocal), IN.normal));
    float3 tangentWORLD = normalize(mul((float3x3)transform.mLocalToWorld, IN.tangent));
    tangentWORLD = normalize(tangentWORLD - normalWORLD * dot(tangentWORLD, normalWORLD));

    output.posWORLD     = posWORLD.xyz;
    output.uv           = IN.uv;
    output.normalWORLD  = normalWORLD;
    output.tangentWORLD = tangentWORLD;
    output.position     = mul(g_Camera.mViewProj, posWORLD);

    // TODO
    output.posPrevCLIP = mul(g_Camera.mViewProjUnjitteredPrev, posWORLD);
    output.posCurrCLIP = mul(g_Camera.mViewProjUnjittered, posWORLD);
    return output;
}