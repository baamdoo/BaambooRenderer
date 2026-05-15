#define _CAMERA
#define _LIGHT
#include "Common.hlsli"
#include "LightCullingCommon.hlsli"


#define MAX_VERTS_PER_LIGHT  192u

#define LT_OFF_SPOT    0u
#define LT_OFF_AREA    1u
#define LT_OFF_SPHERE  2u
#define LT_OFF_DISK    3u
#define LT_OFF_TUBE    4u

// Vert counts per type (must match the comment block above).
#define VC_SPHERE 192u
#define VC_SPOT    96u
#define VC_AREA   138u
#define VC_DISK   162u
#define VC_TUBE   136u

// Ring segmentation
#define SPHERE_SEGS  32u   // per great circle
#define SPOT_RAYS    32u   // apex→rim
#define SPOT_BASE    16u   // base ring segs
#define DISK_SEGS    32u
#define TUBE_SEGS    16u   // end-ring segs

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float3 g_LineColor;       // Per-type color (host passes the matching type's accent)
    float  g_LineAlpha;

    uint   g_TypeMask;        // bit 0=spot, 1=area, 2=sphere, 3=disk, 4=tube
    uint3  g_Padding;
};


struct VSOut
{
    float4 position : SV_Position;
    float4 color    : TEXCOORD0;
};

static const float4 NaN4 = float4(0.0/0.0, 0.0/0.0, 0.0/0.0, 0.0/0.0);

struct LightSlot
{
    uint type;     // LIGHT_TYPE_* (0 → 비활성)
    uint idx;      // type-local index
};

LightSlot DecodeLightSlot(uint instID)
{
    LightSlot s = (LightSlot)0;

    uint nSpot   = ((g_TypeMask >> LT_OFF_SPOT)   & 1u) ? g_Lights.numSpots   : 0u;
    uint nArea   = ((g_TypeMask >> LT_OFF_AREA)   & 1u) ? g_Lights.numAreas   : 0u;
    uint nSphere = ((g_TypeMask >> LT_OFF_SPHERE) & 1u) ? g_Lights.numSpheres : 0u;
    uint nDisk   = ((g_TypeMask >> LT_OFF_DISK)   & 1u) ? g_Lights.numDisks   : 0u;
    uint nTube   = ((g_TypeMask >> LT_OFF_TUBE)   & 1u) ? g_Lights.numTubes   : 0u;

    uint cursor = 0u;
    if (instID < cursor + nSpot)   { s.type = LIGHT_TYPE_SPOT;   s.idx = instID - cursor; return s; }
    cursor += nSpot;
    if (instID < cursor + nArea)   { s.type = LIGHT_TYPE_AREA;   s.idx = instID - cursor; return s; }
    cursor += nArea;
    if (instID < cursor + nSphere) { s.type = LIGHT_TYPE_SPHERE; s.idx = instID - cursor; return s; }
    cursor += nSphere;
    if (instID < cursor + nDisk)   { s.type = LIGHT_TYPE_DISK;   s.idx = instID - cursor; return s; }
    cursor += nDisk;
    if (instID < cursor + nTube)   { s.type = LIGHT_TYPE_TUBE;   s.idx = instID - cursor; return s; }

    return s;
}

// =========================================================================
// Helpers — orthonormal basis from a single direction (Pixar / Frisvall 2012).
//   `axis` must be normalized. Output (axis, tangent, bitangent) is right-handed.
// =========================================================================
void BuildBasis(float3 axis, out float3 t, out float3 b)
{
    // Branchless basis: pick the smallest axis-aligned vector to seed.
    float3 seed = (abs(axis.x) < 0.999) ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
    t = normalize(cross(seed, axis));
    b = cross(axis, t);
}

float3 RingPoint(float3 center, float3 t, float3 b, float radius, float angle)
{
    return center + radius * (cos(angle) * t + sin(angle) * b);
}

// vertID maps a paired endpoint within a "segment ring" (s segments × 2 endpoints).
//   even vertID → start endpoint at segIdx
//   odd  vertID → end endpoint at segIdx+1 (wraps via modulo)
float3 RingVert(float3 center, float3 t, float3 b, float radius, uint vertID, uint segs)
{
    uint seg = vertID >> 1;
    uint dir = vertID &  1u;
    uint k   = (seg + dir) % segs;
    float angle = (float(k) / float(segs)) * 2.0 * PI;
    return RingPoint(center, t, b, radius, angle);
}

// Half-ring: half-circle traced from +e1 through +e2 to -e1 (angle 0..π).
//   localV ∈ [0, 2*segs). Used for hemisphere meridians + capsule end-caps —
//   the half-circle endpoint matches `RingVert(..., segs=2*segs, angle=π)` so the
//   meridian seam meets the equator cleanly at ±e1.
float3 HalfRingVert(float3 center, float3 e1, float3 e2, float radius, uint localV, uint segs)
{
    uint seg = localV >> 1;
    uint dir = localV &  1u;
    uint k   = seg + dir; // 0..segs (no wrap — endpoint is -e1)
    float angle = (float(k) / float(segs)) * PI;
    return center + radius * (cos(angle) * e1 + sin(angle) * e2);
}

// =========================================================================
// Per-type wireframe generation. Each returns a *world-space* position or NaN.
// =========================================================================

// Sphere — 3 great circles, total 192 verts.
float3 SphereVert(uint vertID, float3 center, float radius)
{
    uint circle = vertID / (SPHERE_SEGS * 2u);   // 0..2
    uint localV = vertID % (SPHERE_SEGS * 2u);

    float3 t, b;
    // 3 axis-aligned great circles — yields canonical sphere gizmo (Detroit/Doom).
    if      (circle == 0u) { t = float3(1, 0, 0); b = float3(0, 1, 0); }
    else if (circle == 1u) { t = float3(0, 1, 0); b = float3(0, 0, 1); }
    else                   { t = float3(1, 0, 0); b = float3(0, 0, 1); }

    return RingVert(center, t, b, radius, localV, SPHERE_SEGS);
}

// Spot — apex sphere (32 rays from apex to rim) + 16-seg base ring at the far cone cap.
float3 SpotVert(uint vertID, float3 apex, float3 axis, float halfAngle, float height)
{
    // Rays [0..63]: paired apex/rim (32 rays × 2 endpoints).
    if (vertID < SPOT_RAYS * 2u)
    {
        uint rayIdx = vertID >> 1;
        bool bRim   = (vertID & 1u) != 0u;
        if (!bRim) return apex;

        float angle = (float(rayIdx) / float(SPOT_RAYS)) * 2.0 * PI;
        float3 t, b;
        BuildBasis(axis, t, b);
        float  rimR = height * tan(halfAngle);
        float3 base = apex + axis * height;
        return RingPoint(base, t, b, rimR, angle);
    }

    // Base ring [64..95]: 16 segs × 2 endpoints.
    uint   localV = vertID - SPOT_RAYS * 2u;
    float  rimR   = height * tan(halfAngle);
    float3 base   = apex + axis * height;
    float3 t, b;
    BuildBasis(axis, t, b);
    return RingVert(base, t, b, rimR, localV, SPOT_BASE);
}

// Area — forward hemisphere of influence (radius = rMax) + emitter rect (physical halfW/halfH)
float3 AreaVert(uint vertID, float3 center, float3 tangent, float3 normal,
                float halfW, float halfH, float rMax)
{
    float3 bitangent = cross(normal, tangent);
    float3 forward   = -normal; // emission direction

    // [0..63] equator — great circle in (tangent, bitangent) plane at influence radius
    if (vertID < 64u)
        return RingVert(center, tangent, bitangent, rMax, vertID, 32u);

    // [64..95] meridian in (tangent, forward) plane → +tangent → +forward (emission) → -tangent
    if (vertID < 96u)
        return HalfRingVert(center, tangent, forward, rMax, vertID - 64u, 16u);

    // [96..127] meridian in (bitangent, forward) plane
    if (vertID < 128u)
        return HalfRingVert(center, bitangent, forward, rMax, vertID - 96u, 16u);

    // [128..135] emitter rect — physical dimensions, indicates emitter position/orientation
    if (vertID < 136u)
    {
        uint edge = (vertID - 128u) >> 1;
        uint dir  = (vertID - 128u) &  1u;

        float3 corner[4];
        corner[0] = center - tangent * halfW - bitangent * halfH;
        corner[1] = center + tangent * halfW - bitangent * halfH;
        corner[2] = center + tangent * halfW + bitangent * halfH;
        corner[3] = center - tangent * halfW + bitangent * halfH;
        return corner[(edge + dir) & 3u];
    }

    // [136..137] emission hairline: center → center + forward·R_max·0.4
    if (vertID == 136u) return center;
    if (vertID == 137u) return center + forward * rMax * 0.4;

    return float3(0.0/0.0, 0.0/0.0, 0.0/0.0);
}

// Disk — forward hemisphere of influence + emitter disk (physical radius) + emission hairline.
float3 DiskVert(uint vertID, float3 center, float3 normal, float3 tangent,
                float diskRadius, float rMax)
{
    float3 bitangent = cross(normal, tangent);
    float3 forward   = -normal;

    // [0..63] equator at influence radius
    if (vertID < 64u)
        return RingVert(center, tangent, bitangent, rMax, vertID, 32u);

    // [64..95] meridian (tangent, forward)
    if (vertID < 96u)
        return HalfRingVert(center, tangent, forward, rMax, vertID - 64u, 16u);

    // [96..127] meridian (bitangent, forward)
    if (vertID < 128u)
        return HalfRingVert(center, bitangent, forward, rMax, vertID - 96u, 16u);

    // [128..159] emitter disk ring — physical radius (16 segs)
    if (vertID < 160u)
        return RingVert(center, tangent, bitangent, diskRadius, vertID - 128u, 16u);

    // [160..161] emission hairline
    if (vertID == 160u) return center;
    if (vertID == 161u) return center + forward * rMax * 0.4;

    return float3(0.0/0.0, 0.0/0.0, 0.0/0.0);
}

// Tube — capsule of influence (radius = rMax around segment A-B).
float3 TubeVert(uint vertID, float3 a, float3 b, float rMax)
{
    float3 axis = normalize(b - a);
    float3 t, n;
    BuildBasis(axis, t, n);

    // Cap A — hemisphere pointing in -axis direction (away from B).
    if (vertID < 32u)
        return HalfRingVert(a, t, -axis, rMax, vertID, 16u);
    if (vertID < 64u)
        return HalfRingVert(a, n, -axis, rMax, vertID - 32u, 16u);

    // Cap B — hemisphere pointing in +axis direction (away from A).
    if (vertID < 96u)
        return HalfRingVert(b, t, axis, rMax, vertID - 64u, 16u);
    if (vertID < 128u)
        return HalfRingVert(b, n, axis, rMax, vertID - 96u, 16u);

    // 4 longitudinal connectors at cardinal angles, parallel to axis, offset by R_max.
    if (vertID < 136u)
    {
        uint localV  = vertID - 128u;
        uint lineIdx = localV >> 1;            // 0..3
        bool bEnd    = (localV & 1u) != 0u;
        float angle  = (float(lineIdx) / 4.0) * 2.0 * PI;
        float3 dir   = cos(angle) * t + sin(angle) * n;
        return (bEnd ? b : a) + dir * rMax;
    }

    return float3(0.0/0.0, 0.0/0.0, 0.0/0.0);
}


VSOut main(uint vertID : SV_VertexID, uint instID : SV_InstanceID)
{
    VSOut o = (VSOut)0;

    LightSlot slot = DecodeLightSlot(instID);
    if (slot.type == 0u)
    {
        o.position = NaN4;
        o.color    = float4(0.0, 0.0, 0.0, 0.0);
        return o;
    }

    float3 worldPos = float3(0.0/0.0, 0.0/0.0, 0.0/0.0);

    // Dispatch per-type, drop unused vertID via NaN.
    if (slot.type == LIGHT_TYPE_SPHERE && vertID < VC_SPHERE)
    {
        SphereLight l = g_Lights.spheres[slot.idx];
        float3 c = float3(l.posX, l.posY, l.posZ);
        float  r = InfluenceRadiusIsotropic(l.luminousFluxLm, l.radius);
        worldPos = SphereVert(vertID, c, r);
    }
    else if (slot.type == LIGHT_TYPE_SPOT && vertID < VC_SPOT)
    {
        SpotLight l = g_Lights.spots[slot.idx];
        float3 apex      = float3(l.posX, l.posY, l.posZ);
        float3 axis      = normalize(float3(l.dirX, l.dirY, l.dirZ));
        float  halfAngle = l.outerConeAngleRad;
        float  H         = InfluenceRadiusCone(l.luminousFluxLm, l.outerConeAngleRad, l.radiusM);
        worldPos = SpotVert(vertID, apex, axis, halfAngle, H);
    }
    else if (slot.type == LIGHT_TYPE_AREA && vertID < VC_AREA)
    {
        AreaLight l = g_Lights.areas[slot.idx];
        float3 c         = float3(l.posX, l.posY, l.posZ);
        float3 n         = normalize(float3(l.normalX, l.normalY, l.normalZ));
        float3 t         = normalize(float3(l.tangentX, l.tangentY, l.tangentZ));
        // R_max — matches LightCullingCountCS.hlsl Area-light influence (Lagarde §3.1)
        float  rPhysical = sqrt(l.halfWidth * l.halfWidth + l.halfHeight * l.halfHeight);
        float  rMax      = InfluenceRadiusIsotropic(l.luminousFluxLm, rPhysical);
        worldPos = AreaVert(vertID, c, t, n, l.halfWidth, l.halfHeight, rMax);
    }
    else if (slot.type == LIGHT_TYPE_DISK && vertID < VC_DISK)
    {
        DiskLight l = g_Lights.disks[slot.idx];
        float3 c    = float3(l.posX, l.posY, l.posZ);
        float3 n    = normalize(float3(l.normalX, l.normalY, l.normalZ));
        float3 t    = normalize(float3(l.tangentX, l.tangentY, l.tangentZ));
        float  rMax = InfluenceRadiusIsotropic(l.luminousFluxLm, l.radius);
        worldPos = DiskVert(vertID, c, n, t, l.radius, rMax);
    }
    else if (slot.type == LIGHT_TYPE_TUBE && vertID < VC_TUBE)
    {
        TubeLight l = g_Lights.tubes[slot.idx];
        float3 a    = float3(l.posAX, l.posAY, l.posAZ);
        float3 b    = float3(l.posBX, l.posBY, l.posBZ);
        // Tube influence = capsule of `R_max` around the line segment (matches
        // LightCullingCountCS.hlsl `IntersectAabbCapsule(... capRadius = rMax)`).
        float  rMax = InfluenceRadiusIsotropic(l.luminousFluxLm, l.radius);
        worldPos = TubeVert(vertID, a, b, rMax);
    }
    else
    {
        o.position = NaN4;
        o.color    = float4(0.0, 0.0, 0.0, 0.0);
        return o;
    }

    // World → observer clip. Light wireframes always anchor in *world*, not frozen-view —
    // light positions move with ImGui interaction, gizmo must follow.
    o.position = mul(g_Camera.mViewProj, float4(worldPos, 1.0));
    o.color    = float4(g_LineColor, g_LineAlpha);
    return o;
}
