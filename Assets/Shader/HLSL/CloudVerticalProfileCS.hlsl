#include "Noise.hlsli"
#include "HelperFunctions.hlsli"

RWTexture2D< float > g_VerticalProfileLUT : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 imgSize;
    g_VerticalProfileLUT.GetDimensions(imgSize.x, imgSize.y);

    uint2 pixCoords = dispatchThreadID.xy;
    if (any(pixCoords >= imgSize))
    {
        return;
    }

    float2 uv = (float2(pixCoords) + 0.5f) / float2(imgSize);

    float stratus       = saturate(1.0f - smoothstep(0.0f, 0.1f, (uv.y - 0.1f) * 2.0f));
    float stratoCumulus = saturate(1.0f - smoothstep(0.1f, 0.6f, (uv.y - 0.3f) * 2.0f));
    float cumulus       = saturate(1.0f - smoothstep(0.0f, 0.30f, uv.y - 0.7f));

    float profile = lerp(stratus, stratoCumulus, smoothstep(0.0f, 0.25f, uv.x));
          profile = lerp(profile, cumulus, smoothstep(0.25f, 1.0f, uv.x));

    g_VerticalProfileLUT[pixCoords] = profile;
}