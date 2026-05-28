#include "TerrainCommon.hlsli"

struct PSOutput
{
    float4 GBuffer0 : SV_Target0;  // albedo.rgb + AO.a
    float4 GBuffer1 : SV_Target1;  // Normal.xyz + MaterialID.w
    float3 GBuffer2 : SV_Target2;  // Emissive.rgb
    float4 GBuffer3 : SV_Target3;  // MotionVectors.xy + Roughness.z + Metallic.w
};

#define TERRAIN_MATERIAL_ID 255u

ConstantBuffer< DescriptorHeapIndex > g_HeightmapPS : register(b2, ROOT_CONSTANT_SPACE);


float3 ComputePixelHeightNormal(float2 uv)
{
    Texture2D< float > Heightmap = GetResource(g_HeightmapPS.index);

    const float2 texel = float2(g_Terrain.HeightmapTexel, g_Terrain.HeightmapTexel);
    const float hL = Heightmap.SampleLevel(g_LinearClampSampler, uv - float2(texel.x, 0.0), 0);
    const float hR = Heightmap.SampleLevel(g_LinearClampSampler, uv + float2(texel.x, 0.0), 0);
    const float hD = Heightmap.SampleLevel(g_LinearClampSampler, uv - float2(0.0, texel.y), 0);
    const float hU = Heightmap.SampleLevel(g_LinearClampSampler, uv + float2(0.0, texel.y), 0);

    const float dhdx = (hR - hL) * g_Terrain.HeightRangeMeter / (2.0 * g_Terrain.WorldPerTexel);
    const float dhdz = (hU - hD) * g_Terrain.HeightRangeMeter / (2.0 * g_Terrain.WorldPerTexel);
    return normalize(float3(-dhdx, 1.0, -dhdz));
}

PSOutput main(MSOutput IN)
{
    PSOutput output = (PSOutput)0;

    const float3 surfaceAlbedo    = float3(0.45, 0.38, 0.28);
    const float  surfaceRoughness = 0.85;
    const float3 cliffAlbedo      = float3(0.30, 0.27, 0.22);
    const float  cliffRoughness   = 0.95;

    const float  s         = saturate(IN.skirtBlend);
    const float3 albedo    = lerp(surfaceAlbedo,    cliffAlbedo,    s);
    const float  roughness = lerp(surfaceRoughness, cliffRoughness, s);
    const float  ao        = 1.0;
    const float  metallic  = 0.0;

    const float3 N = ComputePixelHeightNormal(IN.uv);
    output.GBuffer0 = float4(albedo, ao);
    output.GBuffer1 = float4(N, float(TERRAIN_MATERIAL_ID) / 255.0);
    output.GBuffer2 = float3(0.0, 0.0, 0.0);
    output.GBuffer3 = float4(0.0, 0.0, roughness, metallic); // velocity.xy = 0 (placeholder)

    return output;
}
