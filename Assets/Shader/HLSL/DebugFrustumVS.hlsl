#define _CAMERA
#define _FROZENCAMERA
#include "Common.hlsli"


cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float3 g_LineColor;
    float  g_LineAlpha;
};

struct VSOut
{
    float4 position : SV_Position;
    float4 color    : TEXCOORD0;
};

// 12 edges × 2 endpoints. Indices reference the 0..7 corner encoding above.
//   Near rect: 0(–,–) → 1(+,–) → 3(+,+) → 2(–,+) → 0
//   Far  rect: 4       → 5       → 7       → 6       → 4
//   Connectors: 0↔4, 1↔5, 2↔6, 3↔7
static const uint k_FrustumEdges[24] = {
    0, 1,   1, 3,   3, 2,   2, 0,    // near plane (4 edges)
    4, 5,   5, 7,   7, 6,   6, 4,    // far plane  (4 edges)
    0, 4,   1, 5,   2, 6,   3, 7,    // connecting edges (4)
};

VSOut main(uint vertID : SV_VertexID)
{
    VSOut o = (VSOut)0;

    uint cornerIdx = k_FrustumEdges[vertID];
    float ndcX = (cornerIdx & 1u) ? 1.0 : -1.0;
    float ndcY = (cornerIdx & 2u) ? 1.0 : -1.0;
    bool  bFar = (cornerIdx & 4u) != 0u;

    float ndcZ = bFar ? (g_FrozenCamera.zNear / max(g_FrozenCamera.zFar, g_FrozenCamera.zNear * 2.0)) : 1.0;

    // Frozen-NDC → world via the *frozen* camera's inverse view-projection. The
    // resulting world position is the corner of the frozen frustum, anchored in
    // world space regardless of where the observer is.
    float4 worldH = mul(g_FrozenCamera.mViewProjInv, float4(ndcX, ndcY, ndcZ, 1.0));
    float3 worldPos = worldH.xyz / max(worldH.w, EPSILON_MIN);

    // Observer (always-live editor camera at b0) clip-space output → as the user
    // free-flies, the wireframe stays world-anchored at the frozen frustum and
    // appears as a "ghost frustum" overlaid on the live scene.
    o.position = mul(g_Camera.mViewProj, float4(worldPos, 1.0));
    o.color    = float4(g_LineColor, g_LineAlpha);
    return o;
}
