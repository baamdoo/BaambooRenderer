#include "../Common.bsh"

Texture2D                        g_SceneTextures[] : register(t0, space100);
StructuredBuffer< MaterialData > g_Materials       : register(t0, space0);

SamplerState g_Sampler : register(s0, space0);

cbuffer DrawConstants : register(b0)
{
    uint materialID;
};

struct PSInput
{
    float4 position   : SV_Position;
    float2 uv         : TEXCOORD0;
    float3 normal     : NORMAL;
    float3 tangent    : TANGENT;
};

float4 main(PSInput input) : SV_Target
{
    MaterialData material = g_Materials[materialID];

    float3 albedo = float3(1.0, 1.0, 1.0);
    if (material.albedoID != INVALID_INDEX)
    {
        albedo = g_SceneTextures[NonUniformResourceIndex(material.albedoID)].Sample(g_Sampler, input.uv).xyz;
    }

    return float4(albedo, 1.0);
}