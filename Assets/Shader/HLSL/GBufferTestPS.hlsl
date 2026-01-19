#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_TransformIndex;
    uint g_MaterialIndex;
    uint g_MeshletIndex;
};


struct PSInput
{
    float4 position : SV_Position;
    float4 color    : COLOR;
};

struct PSOutput
{
    float4 GBuffer0 : SV_Target0;  // albedo.rgb + AO.a
    float4 GBuffer1 : SV_Target1;  // Normal.xyz + MaterialID.w
    float3 GBuffer2 : SV_Target2;  // Emissive.rgb
    float4 GBuffer3 : SV_Target3;  // MotionVectors.xy + Roughness.z + Metallic.w
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;
    OUT.GBuffer0 = IN.color;
    OUT.GBuffer1 = IN.color;
    OUT.GBuffer2 = IN.color.xyz;
    OUT.GBuffer3 = IN.color;

    return OUT;
}