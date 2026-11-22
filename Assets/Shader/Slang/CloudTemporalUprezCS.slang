#include "ShaderShared/Scene.h"
#include "Common/Binding.slangh"

struct CloudTemporalUprezPush
{
    CloudTemporalUprezConstants constants;

    uint ID_Scene;
    uint ID_CloudScatteringLUT;
    uint ID_PrevUprezzedCloudScatteringLUT;
    uint ID_DepthBuffer;
    uint ID_OutUprezzedCloudScatteringLUT;
};

[[vk::push_constant]] ConstantBuffer< CloudTemporalUprezPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

[numthreads(8, 8, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2D< float4 > OutUprezzedCloudScatteringLUT = GetResource(g_Push.ID_OutUprezzedCloudScatteringLUT);

    uint2 imageSize;
    OutUprezzedCloudScatteringLUT.GetDimensions(imageSize.x, imageSize.y);
    const uint2 pixCoords = tID.xy;
    if (any(pixCoords >= imageSize))
        return;

    const float2 uv = (float2(pixCoords) + 0.5) / float2(imageSize);

    Texture2D< float4 > CloudScatteringLUT = GetResource(g_Push.ID_CloudScatteringLUT);

    const float4 currentColor = CloudScatteringLUT.SampleLevel(g_LinearClampSampler, uv, 0);
    if (g_Push.constants.historyValid == 0)
    {
        OutUprezzedCloudScatteringLUT[pixCoords] = currentColor;
        return;
    }

    Texture2D< float >  DepthBuffer                    = GetResource(g_Push.ID_DepthBuffer);
    Texture2D< float4 > PrevUprezzedCloudScatteringLUT = GetResource(g_Push.ID_PrevUprezzedCloudScatteringLUT);
    StructuredBuffer< SceneGlobals > scene             = GetResource(g_Push.ID_Scene);

    const float depth = DepthBuffer.SampleLevel(g_PointClampSampler, uv, 0);

    // Kept homogeneous: the sky (reverse-Z depth 0) reprojects as a direction at w = 0
    const float4 posCS     = float4(uv.x * 2.0 - 1.0, uv.y * -2.0 + 1.0, depth, 1.0);
    const float4 posWS     = mul(scene[0].camera.mViewProjInv, posCS);
    const float4 posPrevCS = mul(scene[0].camera.mViewProjUnjitteredPrev, posWS);

    const float2 prevUV = posPrevCS.xy / posPrevCS.w * float2(0.5, -0.5) + 0.5;

    float4 newColor = currentColor;
    if (all(prevUV > float2(0.0)) && all(prevUV < float2(1.0)))
    {
        float4 historyColor = PrevUprezzedCloudScatteringLUT.SampleLevel(g_LinearClampSampler, prevUV, 0);
        if (any(isnan(historyColor)))
            historyColor = float4(0.0, 0.0, 0.0, 1.0);

        newColor = lerp(historyColor, currentColor, g_Push.constants.blendAlpha);
    }

    OutUprezzedCloudScatteringLUT[pixCoords] = newColor;
}
