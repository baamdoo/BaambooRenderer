Texture2D< float4 > g_TextureR : register(t0);
Texture2D< float4 > g_TextureG : register(t1);
Texture2D< float4 > g_TextureB : register(t2);

RWTexture2D< float4 > g_CombinedTexture : register(u0);

SamplerState g_LinearSampler : register(s0);

[numthreads(16, 16, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    // Get texture dimensions
    uint width, height;
    g_CombinedTexture.GetDimensions(width, height);

    // Calculate UV coordinates
    float2 texCoords = float2(tID.xy) / float2(width, height);

    // Sample channels
    float R = g_TextureR.SampleLevel(g_LinearSampler, texCoords, 0).r;
    float G = g_TextureG.SampleLevel(g_LinearSampler, texCoords, 0).g;
    float B = g_TextureB.SampleLevel(g_LinearSampler, texCoords, 0).b;

    // Combine and write output
    g_CombinedTexture[tID.xy] = float4(R, G, B, 1.0);
}