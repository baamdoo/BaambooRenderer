#include "ShaderShared/DebugDraw.h"
#include "ShaderShared/Scene.h"
#include "Common/Binding.slangh"
#include "Common/Math.slangh"
#include "Common/DebugDraw.slangh"

struct DebugFrustumPush
{
    DebugFrustumConstants constants;

    uint ID_Scene;
};

[[vk::push_constant]] ConstantBuffer< DebugFrustumPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// 12 edges x 2 endpoints. Corner bits: 0 = +x, 1 = +y, 2 = far plane
static const uint kFrustumEdges[24] = {
    0, 1,   1, 3,   3, 2,   2, 0,
    4, 5,   5, 7,   7, 6,   6, 4,
    0, 4,   1, 5,   2, 6,   3, 7,
};

[shader("vertex")]
DebugVertexOutput main(uint vertexID : SV_VertexID)
{
    StructuredBuffer< SceneGlobals > scene = GetResource(g_Push.ID_Scene);

    const uint corner = kFrustumEdges[vertexID];
    const float ndcX  = (corner & 1u) != 0u ? 1.0 : -1.0;
    const float ndcY  = (corner & 2u) != 0u ? 1.0 : -1.0;
    const bool  bFar  = (corner & 4u) != 0u;

    // Reverse-Z with an infinite far plane: depth = zNear / viewZ, so the near plane is at 1
    const float zNear = scene[0].frozenCamera.zNear;
    const float zFar  = scene[0].frozenCamera.zFar;
    const float ndcZ  = bFar ? zNear / max(zFar, zNear * 2.0) : 1.0;

    // The frozen frustum stays anchored in world space while the live camera moves
    const float4 positionH  = mul(scene[0].frozenCamera.mViewProjInv, float4(ndcX, ndcY, ndcZ, 1.0));
    const float3 positionWS = positionH.xyz / max(positionH.w, EPSILON_MIN);

    DebugVertexOutput output;
    output.position = mul(scene[0].camera.mViewProj, float4(positionWS, 1.0));
    output.color    = float4(g_Push.constants.lineColor, g_Push.constants.lineAlpha);
    return output;
}
