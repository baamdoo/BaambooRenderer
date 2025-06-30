#define _CAMERA
#include "Common.hlsli"

Texture2D                        g_SceneTextures[] : register(t0, space100);
StructuredBuffer< MaterialData > g_Materials       : register(t0, space0);

SamplerState g_LinearSampler : register(s0, space0);

cbuffer RootConstants : register(b1, space0)
{
    uint g_MaterialIndex;
};

struct PSInput
{
    float4 position     : SV_Position;
    float4 posCurrCLIP  : POSITION0;
    float4 posPrevCLIP  : POSITION1;
    float3 posWORLD     : POSITION2;
    float2 uv           : TEXCOORD0;
    float3 normalWORLD  : TEXCOORD1;
    float3 tangentWORLD : TEXCOORD2;
};

struct PSOutput 
{
    float4 GBuffer0 : SV_Target0;  // albedo.rgb + AO.a
    float4 GBuffer1 : SV_Target1;  // Normal.xyz + MaterialID.w
    float3 GBuffer2 : SV_Target2;  // Emissive.rgb
    float4 GBuffer3 : SV_Target3;  // MotionVectors.xy + Roughness.z + Metallic.w
};

PSOutput main(PSInput input)
{
    PSOutput output;
    if (g_MaterialIndex == INVALID_INDEX)
    {
        return output;
    }

    MaterialData material = g_Materials[g_MaterialIndex];

    float3 albedo = float3(1.0, 1.0, 1.0);
    if (material.albedoID != INVALID_INDEX) 
    {
        albedo = g_SceneTextures[NonUniformResourceIndex(material.albedoID)].Sample(g_LinearSampler, input.uv).rgb;
        //albedo = pow(albedo, 2.2);
    }
    albedo *= float3(material.tintR, material.tintG, material.tintB);

    float metallic  = material.metallic;
    float roughness = material.roughness;
    float ao        = 1.0;
    if (material.metallicRoughnessAoID != INVALID_INDEX)
    {
        float3 orm = 
            g_SceneTextures[NonUniformResourceIndex(material.metallicRoughnessAoID)].Sample(g_LinearSampler, input.uv).rgb;

        metallic  *= orm.b;
        roughness *= orm.g;
        ao        *= orm.r;
    }
    roughness = max(roughness, MIN_ROUGHNESS);

    float3 N = normalize(input.normalWORLD);
    if (material.normalID != INVALID_INDEX)
    {
        float3 tangentNormal 
            = g_SceneTextures[NonUniformResourceIndex(material.normalID)].Sample(g_LinearSampler, input.uv).rgb * 2.0 - 1.0;

        float3   T   = normalize(input.tangentWORLD);
        float3   B   = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);

        N = normalize(mul(tangentNormal, TBN));
    }

    float3 emissive = float3(0.0, 0.0, 0.0);
    if (material.emissiveID != INVALID_INDEX)
    {
        emissive  = g_SceneTextures[NonUniformResourceIndex(material.emissiveID)].Sample(g_LinearSampler, input.uv).rgb;
        //emissive  = pow(emissive, 2.2); // convert from sRGB to linear
        emissive *= material.emissivePower;
    }

    output.GBuffer0 = float4(albedo, ao);
    output.GBuffer1 = float4(N, g_MaterialIndex / 255.0);
    output.GBuffer2 = emissive;

    float2 posPrevSCREEN = (input.posPrevCLIP.xy / input.posPrevCLIP.w) * 0.5 + 0.5;
    float2 posCurrSCREEN = (input.posCurrCLIP.xy / input.posCurrCLIP.w) * 0.5 + 0.5;
    output.GBuffer3 = float4(posCurrSCREEN - posPrevSCREEN, roughness, metallic);

    return output;
}