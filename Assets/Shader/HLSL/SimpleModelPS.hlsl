#include "../Common.bsh"

Texture2D                        g_SceneTextures[] : register(t0, space100);
StructuredBuffer< MaterialData > g_Materials       : register(t0, space0);

SamplerState g_Sampler : register(s0, space0);

cbuffer RootConstants : register(b0, space0)
{
    uint MaterialIndex;
};

struct PSInput
{
    float4 posCLIP      : SV_Position;
    float2 uv           : TEXCOORD0;
    float3 normalWORLD  : TEXCOORD1;
    float3 tangentWORLD : TEXCOORD2;
};

float4 main(PSInput input) : SV_Target
{
    if (MaterialIndex == INVALID_INDEX)
    {
        return float4(1.0, 0.0, 0.0, 1.0);
    }

    MaterialData material = g_Materials[MaterialIndex];

    float3 albedo = float3(1.0, 1.0, 1.0);
    if (material.albedoID != INVALID_INDEX)
    {
        albedo = g_SceneTextures[NonUniformResourceIndex(material.albedoID)].Sample(g_Sampler, input.uv).xyz;
    }

    return float4(albedo, 1.0);
}