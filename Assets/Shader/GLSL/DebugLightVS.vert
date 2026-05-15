#version 460
#extension GL_GOOGLE_include_directive : require

#define _CAMERA
#define _LIGHT
#include "DescriptorCommon.hg"
#include "LightCullingCommon.hg"


#define MAX_VERTS_PER_LIGHT  192u

#define LT_OFF_SPOT    0u
#define LT_OFF_AREA    1u
#define LT_OFF_SPHERE  2u
#define LT_OFF_DISK    3u
#define LT_OFF_TUBE    4u

// Vert counts per type
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


layout(push_constant) uniform PushConstants
{
    vec3  lineColor;       // Per-type color (host passes the matching type's accent)
    float lineAlpha;

    uint  typeMask;        // bit 0=spot, 1=area, 2=sphere, 3=disk, 4=tube
    uvec3 padding0;
} g_Push;


layout(location = 0) out vec4 v_Color;


float NaN()
{
    return uintBitsToFloat(0x7FC00000u);
}

struct LightSlot
{
    uint type;     // LIGHT_TYPE_* (0 → 비활성)
    uint idx;      // type-local index
};

LightSlot DecodeLightSlot(uint instID)
{
    LightSlot s;
    s.type = 0u;
    s.idx  = 0u;

    uint nSpot   = ((g_Push.typeMask >> LT_OFF_SPOT)   & 1u) != 0u ? g_LightBuffer.lightingData.numSpots   : 0u;
    uint nArea   = ((g_Push.typeMask >> LT_OFF_AREA)   & 1u) != 0u ? g_LightBuffer.lightingData.numAreas   : 0u;
    uint nSphere = ((g_Push.typeMask >> LT_OFF_SPHERE) & 1u) != 0u ? g_LightBuffer.lightingData.numSpheres : 0u;
    uint nDisk   = ((g_Push.typeMask >> LT_OFF_DISK)   & 1u) != 0u ? g_LightBuffer.lightingData.numDisks   : 0u;
    uint nTube   = ((g_Push.typeMask >> LT_OFF_TUBE)   & 1u) != 0u ? g_LightBuffer.lightingData.numTubes   : 0u;

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
// Helpers — orthonormal basis from a single direction. `axis` must be normalized.
// =========================================================================
void BuildBasis(vec3 axis, out vec3 t, out vec3 b)
{
    vec3 seed = (abs(axis.x) < 0.999) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    t = normalize(cross(seed, axis));
    b = cross(axis, t);
}

vec3 RingPoint(vec3 center, vec3 t, vec3 b, float radius, float angle)
{
    return center + radius * (cos(angle) * t + sin(angle) * b);
}

// vertID maps a paired endpoint within a "segment ring" (s segments × 2 endpoints).
vec3 RingVert(vec3 center, vec3 t, vec3 b, float radius, uint vertID, uint segs)
{
    uint seg = vertID >> 1;
    uint dir = vertID &  1u;
    uint k   = (seg + dir) % segs;
    float angle = (float(k) / float(segs)) * 2.0 * PI;
    return RingPoint(center, t, b, radius, angle);
}

// Half-ring: half-circle traced from +e1 through +e2 to -e1 (angle 0..π).
vec3 HalfRingVert(vec3 center, vec3 e1, vec3 e2, float radius, uint localV, uint segs)
{
    uint seg = localV >> 1;
    uint dir = localV &  1u;
    uint k   = seg + dir; // 0..segs (no wrap — endpoint is -e1)
    float angle = (float(k) / float(segs)) * PI;
    return center + radius * (cos(angle) * e1 + sin(angle) * e2);
}

// =========================================================================
// Per-type wireframe generation.
// =========================================================================

vec3 SphereVert(uint vertID, vec3 center, float radius)
{
    uint circle = vertID / (SPHERE_SEGS * 2u);
    uint localV = vertID % (SPHERE_SEGS * 2u);

    vec3 t, b;
    if      (circle == 0u) { t = vec3(1.0, 0.0, 0.0); b = vec3(0.0, 1.0, 0.0); }
    else if (circle == 1u) { t = vec3(0.0, 1.0, 0.0); b = vec3(0.0, 0.0, 1.0); }
    else                   { t = vec3(1.0, 0.0, 0.0); b = vec3(0.0, 0.0, 1.0); }

    return RingVert(center, t, b, radius, localV, SPHERE_SEGS);
}

vec3 SpotVert(uint vertID, vec3 apex, vec3 axis, float halfAngle, float height)
{
    // Rays [0..63]: paired apex/rim
    if (vertID < SPOT_RAYS * 2u)
    {
        uint rayIdx = vertID >> 1;
        bool bRim   = (vertID & 1u) != 0u;
        if (!bRim) return apex;

        float angle = (float(rayIdx) / float(SPOT_RAYS)) * 2.0 * PI;
        vec3 t, b;
        BuildBasis(axis, t, b);
        float rimR = height * tan(halfAngle);
        vec3  base = apex + axis * height;
        return RingPoint(base, t, b, rimR, angle);
    }

    // Base ring [64..95]
    uint  localV = vertID - SPOT_RAYS * 2u;
    float rimR   = height * tan(halfAngle);
    vec3  base   = apex + axis * height;
    vec3 t, b;
    BuildBasis(axis, t, b);
    return RingVert(base, t, b, rimR, localV, SPOT_BASE);
}

vec3 AreaVert(uint vertID, vec3 center, vec3 tangent, vec3 normal,
              float halfW, float halfH, float rMax)
{
    vec3 bitangent = cross(normal, tangent);
    vec3 forward   = -normal; // emission direction

    if (vertID < 64u)
        return RingVert(center, tangent, bitangent, rMax, vertID, 32u);

    if (vertID < 96u)
        return HalfRingVert(center, tangent, forward, rMax, vertID - 64u, 16u);

    if (vertID < 128u)
        return HalfRingVert(center, bitangent, forward, rMax, vertID - 96u, 16u);

    if (vertID < 136u)
    {
        uint edge = (vertID - 128u) >> 1;
        uint dir  = (vertID - 128u) &  1u;

        vec3 corner[4];
        corner[0] = center - tangent * halfW - bitangent * halfH;
        corner[1] = center + tangent * halfW - bitangent * halfH;
        corner[2] = center + tangent * halfW + bitangent * halfH;
        corner[3] = center - tangent * halfW + bitangent * halfH;
        return corner[(edge + dir) & 3u];
    }

    if (vertID == 136u) return center;
    if (vertID == 137u) return center + forward * rMax * 0.4;

    return vec3(NaN(), NaN(), NaN());
}

vec3 DiskVert(uint vertID, vec3 center, vec3 normal, vec3 tangent,
              float diskRadius, float rMax)
{
    vec3 bitangent = cross(normal, tangent);
    vec3 forward   = -normal;

    if (vertID < 64u)
        return RingVert(center, tangent, bitangent, rMax, vertID, 32u);

    if (vertID < 96u)
        return HalfRingVert(center, tangent, forward, rMax, vertID - 64u, 16u);

    if (vertID < 128u)
        return HalfRingVert(center, bitangent, forward, rMax, vertID - 96u, 16u);

    if (vertID < 160u)
        return RingVert(center, tangent, bitangent, diskRadius, vertID - 128u, 16u);

    if (vertID == 160u) return center;
    if (vertID == 161u) return center + forward * rMax * 0.4;

    return vec3(NaN(), NaN(), NaN());
}

vec3 TubeVert(uint vertID, vec3 a, vec3 b, float rMax)
{
    vec3 axis = normalize(b - a);
    vec3 t, n;
    BuildBasis(axis, t, n);

    // Cap A — hemisphere pointing in -axis direction
    if (vertID < 32u)
        return HalfRingVert(a, t, -axis, rMax, vertID, 16u);
    if (vertID < 64u)
        return HalfRingVert(a, n, -axis, rMax, vertID - 32u, 16u);

    // Cap B — hemisphere pointing in +axis direction
    if (vertID < 96u)
        return HalfRingVert(b, t, axis, rMax, vertID - 64u, 16u);
    if (vertID < 128u)
        return HalfRingVert(b, n, axis, rMax, vertID - 96u, 16u);

    // 4 longitudinal connectors at cardinal angles
    if (vertID < 136u)
    {
        uint localV  = vertID - 128u;
        uint lineIdx = localV >> 1;
        bool bEnd    = (localV & 1u) != 0u;
        float angle  = (float(lineIdx) / 4.0) * 2.0 * PI;
        vec3 dir     = cos(angle) * t + sin(angle) * n;
        return (bEnd ? b : a) + dir * rMax;
    }

    return vec3(NaN(), NaN(), NaN());
}


void main()
{
    uint vertID = uint(gl_VertexIndex);
    uint instID = uint(gl_InstanceIndex);

    LightSlot slot = DecodeLightSlot(instID);
    if (slot.type == 0u)
    {
        gl_Position = vec4(NaN(), NaN(), NaN(), NaN());
        v_Color     = vec4(0.0);
        return;
    }

    vec3 worldPos = vec3(NaN(), NaN(), NaN());

    // Dispatch per-type, drop unused vertID via NaN.
    if (slot.type == LIGHT_TYPE_SPHERE && vertID < VC_SPHERE)
    {
        SphereLight l = g_LightBuffer.lightingData.spheres[slot.idx];
        vec3  c = vec3(l.posX, l.posY, l.posZ);
        float r = InfluenceRadiusIsotropic(l.luminousFluxLm, l.radius);
        worldPos = SphereVert(vertID, c, r);
    }
    else if (slot.type == LIGHT_TYPE_SPOT && vertID < VC_SPOT)
    {
        SpotLight l = g_LightBuffer.lightingData.spots[slot.idx];
        vec3  apex      = vec3(l.posX, l.posY, l.posZ);
        vec3  axis      = normalize(vec3(l.dirX, l.dirY, l.dirZ));
        float halfAngle = l.outerConeAngleRad;
        float H         = InfluenceRadiusCone(l.luminousFluxLm, l.outerConeAngleRad, l.radiusM);
        worldPos = SpotVert(vertID, apex, axis, halfAngle, H);
    }
    else if (slot.type == LIGHT_TYPE_AREA && vertID < VC_AREA)
    {
        AreaLight l = g_LightBuffer.lightingData.areas[slot.idx];
        vec3  c         = vec3(l.posX, l.posY, l.posZ);
        vec3  n         = normalize(vec3(l.normalX, l.normalY, l.normalZ));
        vec3  t         = normalize(vec3(l.tangentX, l.tangentY, l.tangentZ));
        float rPhysical = sqrt(l.halfWidth * l.halfWidth + l.halfHeight * l.halfHeight);
        float rMax      = InfluenceRadiusIsotropic(l.luminousFluxLm, rPhysical);
        worldPos = AreaVert(vertID, c, t, n, l.halfWidth, l.halfHeight, rMax);
    }
    else if (slot.type == LIGHT_TYPE_DISK && vertID < VC_DISK)
    {
        DiskLight l = g_LightBuffer.lightingData.disks[slot.idx];
        vec3  c    = vec3(l.posX, l.posY, l.posZ);
        vec3  n    = normalize(vec3(l.normalX, l.normalY, l.normalZ));
        vec3  t    = normalize(vec3(l.tangentX, l.tangentY, l.tangentZ));
        float rMax = InfluenceRadiusIsotropic(l.luminousFluxLm, l.radius);
        worldPos = DiskVert(vertID, c, n, t, l.radius, rMax);
    }
    else if (slot.type == LIGHT_TYPE_TUBE && vertID < VC_TUBE)
    {
        TubeLight l = g_LightBuffer.lightingData.tubes[slot.idx];
        vec3  a    = vec3(l.posAX, l.posAY, l.posAZ);
        vec3  b    = vec3(l.posBX, l.posBY, l.posBZ);
        float rMax = InfluenceRadiusIsotropic(l.luminousFluxLm, l.radius);
        worldPos = TubeVert(vertID, a, b, rMax);
    }
    else
    {
        gl_Position = vec4(NaN(), NaN(), NaN(), NaN());
        v_Color     = vec4(0.0);
        return;
    }

    // World → observer clip. Light wireframes always anchor in world (not frozen-view) — light positions move with ImGui interaction.
    gl_Position = g_Camera.mViewProj * vec4(worldPos, 1.0);
    v_Color     = vec4(g_Push.lineColor, g_Push.lineAlpha);
}
