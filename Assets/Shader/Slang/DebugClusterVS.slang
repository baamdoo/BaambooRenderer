#include "ShaderShared/DebugDraw.h"
#include "ShaderShared/Light.h"
#include "ShaderShared/Scene.h"
#include "Common/Binding.slangh"
#include "Common/Math.slangh"
#include "Common/DebugDraw.slangh"

struct DebugClusterPush
{
    DebugClusterConstants constants;

    uint ID_Scene;
    uint ID_ClusterBuffer;
    uint ID_LightGridBuffer;
};

[[vk::push_constant]] ConstantBuffer< DebugClusterPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// 12 edges x 2 endpoints. Corner bits: 0 = max.x, 1 = max.y, 2 = max.z
static const uint kAabbEdges[24] = {
    0, 1,   1, 3,   3, 2,   2, 0,
    4, 5,   5, 7,   7, 6,   6, 4,
    0, 4,   1, 5,   2, 6,   3, 7,
};

// Reference: https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f
float3 TurboColormap(float t)
{
    t = saturate(t);

    const float4 kRedVec4   = float4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    const float4 kGreenVec4 = float4(0.09140261, 2.19418839,   4.84296658, -14.18503333);
    const float4 kBlueVec4  = float4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    const float2 kRedVec2   = float2(-152.94239396, 59.28637943);
    const float2 kGreenVec2 = float2(  4.27729857,  2.82956604);
    const float2 kBlueVec2  = float2(-89.90310912, 27.34824973);

    const float4 v4 = float4(1.0, t, t * t, t * t * t);
    const float2 v2 = v4.zw * v4.z;

    return float3(
        dot(v4, kRedVec4)   + dot(v2, kRedVec2),
        dot(v4, kGreenVec4) + dot(v2, kGreenVec2),
        dot(v4, kBlueVec4)  + dot(v2, kBlueVec2));
}

[shader("vertex")]
DebugVertexOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    DebugVertexOutput output;
    output.position = kCulledClipPosition;
    output.color    = float4(0.0);

    const DebugClusterConstants constants = g_Push.constants;
    if (instanceID >= constants.numTilesX * constants.numTilesY * constants.numSlices)
        return output;

    StructuredBuffer< SceneGlobals > scene     = GetResource(g_Push.ID_Scene);
    StructuredBuffer< ClusterAABB >  clusters  = GetResource(g_Push.ID_ClusterBuffer);
    StructuredBuffer< uint2 >        lightGrid = GetResource(g_Push.ID_LightGridBuffer);

    const uint lightCount = lightGrid[instanceID].y;
    if ((constants.flagsBits & kDebugClusterFlagSkipEmpty) != 0u && lightCount == 0u)
        return output;

    const ClusterAABB cluster = clusters[instanceID];
    const uint corner = kAabbEdges[vertexID];
    const float3 cornerVS = float3(
        (corner & 1u) != 0u ? cluster.aabbMax.x : cluster.aabbMin.x,
        (corner & 2u) != 0u ? cluster.aabbMax.y : cluster.aabbMin.y,
        (corner & 4u) != 0u ? cluster.aabbMax.z : cluster.aabbMin.z);

    // Frozen view space to world: mViewProjInv * mProj = mView^-1
    const float4 frozenClip = mul(scene[0].frozenCamera.mProj, float4(cornerVS, 1.0));
    const float4 positionH  = mul(scene[0].frozenCamera.mViewProjInv, frozenClip);
    const float3 positionWS = positionH.xyz / max(positionH.w, EPSILON_MIN);

    float3 color = constants.lineColor;
    if ((constants.flagsBits & kDebugClusterFlagHeatmap) != 0u)
        color = TurboColormap(float(lightCount) / float(max(constants.saturationMax, 1u)));

    output.position = mul(scene[0].camera.mViewProj, float4(positionWS, 1.0));
    output.color    = float4(color, constants.lineAlpha);
    return output;
}
