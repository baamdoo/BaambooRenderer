#define _MATERIAL
#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_TransformIndex;
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

    StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);

    MaterialData material = Materials[g_MaterialIndex];

    float3 albedo = float3(1.0, 1.0, 1.0);
    if (material.albedoID != INVALID_INDEX) 
    {
        Texture2D AlbedoMap = GetResource(material.albedoID);

        albedo = AlbedoMap.Sample(g_LinearClampSampler, input.uv).rgb;
        //albedo = pow(albedo, 2.2);
    }
    albedo *= float3(material.tintR, material.tintG, material.tintB);

    float metallic  = material.metallic;
    float roughness = material.roughness;
    float ao        = 1.0;
    if (material.metallicRoughnessAoID != INVALID_INDEX)
    {
        Texture2D OrmMap = GetResource(material.metallicRoughnessAoID);

        float3 orm = OrmMap.Sample(g_LinearClampSampler, input.uv).rgb;

        metallic  *= orm.b;
        roughness *= orm.g;
        ao        *= orm.r;
    }

    float3 N = normalize(input.normalWORLD);
    if (material.normalID != INVALID_INDEX)
    {
        Texture2D NormalMap = GetResource(material.normalID);

        float3 tangentNormal = NormalMap.Sample(g_LinearClampSampler, input.uv).rgb * 2.0 - 1.0;

        float3   T   = normalize(input.tangentWORLD);
        float3   B   = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);

        N = normalize(mul(tangentNormal, TBN));
    }

    float3 emissive = float3(0.0, 0.0, 0.0);
    if (material.emissiveID != INVALID_INDEX)
    {
        Texture2D EmissiveMap = GetResource(material.emissiveID);

        emissive = EmissiveMap.Sample(g_LinearClampSampler, input.uv).rgb;
        //emissive  = pow(emissive, 2.2); // convert from sRGB to linear
        emissive *= material.emissivePower;
    }

    output.GBuffer0 = float4(albedo, ao);
    output.GBuffer1 = float4(N, g_MaterialIndex / 255.0);
    output.GBuffer2 = emissive;

    float2 posPrevUV = (input.posPrevCLIP.xy / input.posPrevCLIP.w) * 0.5 + 0.5;
    float2 posCurrUV = (input.posCurrCLIP.xy / input.posCurrCLIP.w) * 0.5 + 0.5;
    output.GBuffer3 = float4(posCurrUV - posPrevUV, roughness, metallic);

    return output;
}