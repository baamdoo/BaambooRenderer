#define _CAMERA
#define _FROZENCAMERA
#include "Common.hlsli"
#include "DebugColor.hlsli"


cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    // Wire color (used when bHeatmap == 0)
    float3 g_LineColor;
    float  g_LineAlpha;

    uint  g_NumTilesX;
    uint  g_NumTilesY;
    uint  g_NumSlices;
    uint  g_FlagsBits;       // bit 0 = bHeatmap, bit 1 = bSkipEmpty

    uint g_SaturationMax;    // ≥ 1 (UI 1..64 slider)
    float3 g_Padding;
};

ConstantBuffer< DescriptorHeapIndex > g_ClusterBuffer   : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_LightGridBuffer : register(b2, ROOT_CONSTANT_SPACE);


struct VSOut
{
    float4 position : SV_Position;
    float4 color    : TEXCOORD0;
};

// 12 edges × 2 endpoints. Indices reference the 0..7 AABB corner encoding:
//   bit 0 = x sign (0 → min.x, 1 → max.x)
//   bit 1 = y sign (0 → min.y, 1 → max.y)
//   bit 2 = z sign (0 → min.z, 1 → max.z)
static const uint k_AabbEdges[24] = {
    0, 1,   1, 3,   3, 2,   2, 0,    // -Z face (4 edges)
    4, 5,   5, 7,   7, 6,   6, 4,    // +Z face (4 edges)
    0, 4,   1, 5,   2, 6,   3, 7,    // connecting edges (4)
};

VSOut main(uint vertID : SV_VertexID, uint instID : SV_InstanceID)
{
    VSOut o = (VSOut)0;

    // Out-of-range instance guard (defensive — host should clamp instanceCount).
    uint clusterCount = g_NumTilesX * g_NumTilesY * g_NumSlices;
    if (instID >= clusterCount)
    {
        o.position = float4(0.0/0.0, 0.0/0.0, 0.0/0.0, 0.0/0.0); // NaN → clipper drop
        o.color    = float4(0.0, 0.0, 0.0, 0.0);
        return o;
    }

    StructuredBuffer< ClusterAABB > Clusters  = GetResource(g_ClusterBuffer.index);
    StructuredBuffer< uint2 >       LightGrid = GetResource(g_LightGridBuffer.index);

    ClusterAABB cluster = Clusters[instID];
    uint        count   = LightGrid[instID].y;

    bool bHeatmap   = (g_FlagsBits & 1u) != 0u;
    bool bSkipEmpty = (g_FlagsBits & 2u) != 0u;

    // Skip empty cluster (visual noise reduction). NaN positions are clipped per-line works because line topology drops the entire line if either endpoint is NaN.
    if (bSkipEmpty && count == 0u)
    {
        o.position = float4(0.0/0.0, 0.0/0.0, 0.0/0.0, 0.0/0.0);
        o.color    = float4(0.0, 0.0, 0.0, 0.0);
        return o;
    }

    // AABB corner from vertID.
    uint cornerIdx = k_AabbEdges[vertID];
    float3 vsMin = float3(cluster.minX, cluster.minY, cluster.minZ);
    float3 vsMax = float3(cluster.maxX, cluster.maxY, cluster.maxZ);
    float3 vsCorner = float3(
        (cornerIdx & 1u) ? vsMax.x : vsMin.x,
        (cornerIdx & 2u) ? vsMax.y : vsMin.y,
        (cornerIdx & 4u) ? vsMax.z : vsMin.z);

    // frozen view-space → frozen NDC → world. mProj ∘ mViewProjInv == mView^-1.
    float4 frozenClip = mul(g_FrozenCamera.mProj, float4(vsCorner, 1.0));
    float4 worldH     = mul(g_FrozenCamera.mViewProjInv, frozenClip);
    float3 worldPos   = worldH.xyz / max(worldH.w, EPSILON_MIN);

    // World → observer clip — wireframe stays anchored at the frozen grid as the
    // observer free-flies, exactly matching the Frustum wireframe's anchoring.
    o.position = mul(g_Camera.mViewProj, float4(worldPos, 1.0));

    // Color: Turbo heatmap when on, otherwise uniform push-constant color.
    float3 rgb = g_LineColor;
    if (bHeatmap)
    {
        float t = saturate(float(count) / float(max(g_SaturationMax, 1u)));
        rgb = TurboColormap(t);
    }
    o.color = float4(rgb, g_LineAlpha);
    return o;
}
