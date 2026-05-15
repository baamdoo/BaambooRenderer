#version 460
#extension GL_GOOGLE_include_directive : require

#define _CAMERA
#define _FROZENCAMERA
#include "DescriptorCommon.hg"


layout(push_constant) uniform PushConstants
{
    vec3  lineColor;
    float lineAlpha;
} g_Push;


layout(location = 0) out vec4 v_Color;


// 12 edges × 2 endpoints. Indices reference the 0..7 corner encoding:
//   bit 0 = x sign (0 → min.x, 1 → max.x)
//   bit 1 = y sign (0 → min.y, 1 → max.y)
//   bit 2 = z sign (0 → near, 1 → far)
const uint k_FrustumEdges[24] = uint[24](
    0u, 1u,   1u, 3u,   3u, 2u,   2u, 0u,    // near rect
    4u, 5u,   5u, 7u,   7u, 6u,   6u, 4u,    // far  rect
    0u, 4u,   1u, 5u,   2u, 6u,   3u, 7u     // connecting edges
);

void main()
{
    uint cornerIdx = k_FrustumEdges[gl_VertexIndex];
    float ndcX = (cornerIdx & 1u) != 0u ? 1.0 : -1.0;
    float ndcY = (cornerIdx & 2u) != 0u ? 1.0 : -1.0;
    bool  bFar = (cornerIdx & 4u) != 0u;

    float ndcZ = bFar ? (g_FrozenCamera.zNear / max(g_FrozenCamera.zFar, g_FrozenCamera.zNear * 2.0)) : 1.0;

    // Frozen-NDC → world via the *frozen* camera's inverse view-projection. The resulting world position is
    // the corner of the frozen frustum, anchored in world space regardless of where the observer is.
    vec4 worldH   = g_FrozenCamera.mViewProjInv * vec4(ndcX, ndcY, ndcZ, 1.0);
    vec3 worldPos = worldH.xyz / max(worldH.w, EPSILON_MIN);

    // Observer (always-live editor camera at b0) clip-space output → ghost frustum overlay.
    gl_Position = g_Camera.mViewProj * vec4(worldPos, 1.0);
    v_Color     = vec4(g_Push.lineColor, g_Push.lineAlpha);
}
