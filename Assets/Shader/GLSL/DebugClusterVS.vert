#version 460
#extension GL_GOOGLE_include_directive : require

#define _CAMERA
#define _FROZENCAMERA
#include "DescriptorCommon.hg"
#include "HelperFunctions.hg"
#include "DebugColor.hg"


layout(push_constant) uniform PushConstants
{
    // Wire color (used when bHeatmap == 0)
    vec3  lineColor;
    float lineAlpha;

    uint  numTilesX;
    uint  numTilesY;
    uint  numSlices;
    uint  flagsBits;       // bit 0 = bHeatmap, bit 1 = bSkipEmpty

    uint  saturationMax;   // ≥ 1 (UI 1..64 slider)
    vec3  padding0;
} g_Push;

layout(set = 1, binding = 0) readonly buffer ClusterAABBBuffer
{
    ClusterAABB clusters[];
} g_ClusterBuffer;

layout(set = 1, binding = 1) readonly buffer LightGridBuffer
{
    uvec2 grid[];
} g_LightGridBuffer;


layout(location = 0) out vec4 v_Color;


// 12 edges × 2 endpoints. Indices reference the 0..7 AABB corner encoding:
//   bit 0 = x sign (0 → min.x, 1 → max.x)
//   bit 1 = y sign (0 → min.y, 1 → max.y)
//   bit 2 = z sign (0 → min.z, 1 → max.z)
const uint k_AabbEdges[24] = uint[24](
    0u, 1u,   1u, 3u,   3u, 2u,   2u, 0u,    // -Z face (4 edges)
    4u, 5u,   5u, 7u,   7u, 6u,   6u, 4u,    // +Z face (4 edges)
    0u, 4u,   1u, 5u,   2u, 6u,   3u, 7u     // connecting edges
);

float NaN()
{
    return uintBitsToFloat(0x7FC00000u);
}

void main()
{
    uint instID = uint(gl_InstanceIndex);

    // Out-of-range instance guard (defensive — host should clamp instanceCount).
    uint clusterCount = g_Push.numTilesX * g_Push.numTilesY * g_Push.numSlices;
    if (instID >= clusterCount)
    {
        gl_Position = vec4(NaN(), NaN(), NaN(), NaN()); // NaN → line clipper drop
        v_Color     = vec4(0.0);
        return;
    }

    ClusterAABB cluster = g_ClusterBuffer.clusters[instID];
    uint        count   = g_LightGridBuffer.grid[instID].y;

    bool bHeatmap   = (g_Push.flagsBits & 1u) != 0u;
    bool bSkipEmpty = (g_Push.flagsBits & 2u) != 0u;

    // Skip empty cluster (visual noise reduction). Vulkan line-topology drops a line when either endpoint is NaN.
    if (bSkipEmpty && count == 0u)
    {
        gl_Position = vec4(NaN(), NaN(), NaN(), NaN());
        v_Color     = vec4(0.0);
        return;
    }

    // AABB corner from gl_VertexIndex.
    uint cornerIdx = k_AabbEdges[gl_VertexIndex];
    vec3 vsMin = vec3(cluster.minX, cluster.minY, cluster.minZ);
    vec3 vsMax = vec3(cluster.maxX, cluster.maxY, cluster.maxZ);
    vec3 vsCorner = vec3(
        (cornerIdx & 1u) != 0u ? vsMax.x : vsMin.x,
        (cornerIdx & 2u) != 0u ? vsMax.y : vsMin.y,
        (cornerIdx & 4u) != 0u ? vsMax.z : vsMin.z);

    // frozen view-space → frozen NDC → world. mProj ∘ mViewProjInv == mView^-1.
    vec4 frozenClip = g_FrozenCamera.mProj * vec4(vsCorner, 1.0);
    vec4 worldH     = g_FrozenCamera.mViewProjInv * frozenClip;
    vec3 worldPos   = worldH.xyz / max(worldH.w, EPSILON_MIN);

    // World → observer clip — wireframe stays anchored at the frozen grid as the observer free-flies.
    gl_Position = g_Camera.mViewProj * vec4(worldPos, 1.0);

    // Color: Turbo heatmap when on, otherwise uniform push-constant color.
    vec3 rgb = g_Push.lineColor;
    if (bHeatmap)
    {
        float t = clamp(float(count) / float(max(g_Push.saturationMax, 1u)), 0.0, 1.0);
        rgb = TurboColormap(t);
    }
    v_Color = vec4(rgb, g_Push.lineAlpha);
}
