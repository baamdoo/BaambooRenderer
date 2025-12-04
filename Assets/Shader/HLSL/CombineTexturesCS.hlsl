#include "Common.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_TextureR           : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TextureG           : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TextureB           : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutCombinedTexture : register(b4, ROOT_CONSTANT_SPACE);

[numthreads(16, 16, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2D< float4 > OutCombinedTexture = GetResource(g_OutCombinedTexture.index);

    // Get texture dimensions
    uint width, height;
    OutCombinedTexture.GetDimensions(width, height);

    // Calculate UV coordinates
    float2 texCoords = float2(tID.xy) / float2(width, height);

    Texture2D< float4 > TextureR = GetResource(g_TextureR.index);
    Texture2D< float4 > TextureG = GetResource(g_TextureG.index);
    Texture2D< float4 > TextureB = GetResource(g_TextureB.index);

    // Sample channels
    float R = TextureR.SampleLevel(g_LinearClampSampler, texCoords, 0).r;
    float G = TextureG.SampleLevel(g_LinearClampSampler, texCoords, 0).g;
    float B = TextureB.SampleLevel(g_LinearClampSampler, texCoords, 0).b;

    // Combine and write output
    OutCombinedTexture[tID.xy] = float4(R, G, B, 1.0);
}