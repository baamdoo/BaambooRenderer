#include "Common/Binding.slangh"
#include "Common/Math.slangh"

struct StaticSkyboxPush
{
    uint ID_SkyboxTexture;
    uint ID_OutSkyboxLUT;
};

[[vk::push_constant]] ConstantBuffer< StaticSkyboxPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Must match the face layout in AtmosphereSkyboxCS
float3 GetRayDirectionFromCubemapCoord(uint3 tID, uint width, uint height)
{
    const float2 uv  = (float2(tID.xy) + 0.5) / float2(width, height);
    const float2 ndc = uv * 2.0 - 1.0;

    float3 rayDir = float3(0.0);
    switch (tID.z)
    {
    case 0: rayDir = float3(1.0, -ndc.y, -ndc.x);  break; // +X
    case 1: rayDir = float3(-1.0, -ndc.y, ndc.x);  break; // -X
    case 2: rayDir = float3(ndc.x, 1.0, ndc.y);    break; // +Y
    case 3: rayDir = float3(ndc.x, -1.0, -ndc.y);  break; // -Y
    case 4: rayDir = float3(ndc.x, -ndc.y, 1.0);   break; // +Z
    case 5: rayDir = float3(-ndc.x, -ndc.y, -1.0); break; // -Z
    }

    return normalize(rayDir);
}

float2 GetEquirectangularUV(float3 rayDir)
{
    float2 uv = float2(atan2Fast(rayDir.z, rayDir.x) / (2.0 * PI), asin(clamp(rayDir.y, -1.0, 1.0)) / PI);
    uv += 0.5;

    uv.y = 1.0 - uv.y;
    return uv;
}

[numthreads(8, 8, 6)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2DArray< float3 > OutSkyboxLUT = GetResource(g_Push.ID_OutSkyboxLUT);

    uint width, height, numFaces;
    OutSkyboxLUT.GetDimensions(width, height, numFaces);
    if (tID.x >= width || tID.y >= height)
        return;

    Texture2D< float4 > SkyboxTexture = GetResource(g_Push.ID_SkyboxTexture);

    const float3 rayDir = GetRayDirectionFromCubemapCoord(tID, width, height);
    const float2 skyUV  = GetEquirectangularUV(rayDir);

    OutSkyboxLUT[tID] = SkyboxTexture.SampleLevel(g_LinearClampSampler, skyUV, 0).rgb;
}
