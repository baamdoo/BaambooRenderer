#include "ShaderShared/DebugDraw.h"
#include "ShaderShared/Light.h"
#include "ShaderShared/Scene.h"
#include "Common/Binding.slangh"
#include "Common/Math.slangh"
#include "Common/Lights.slangh"
#include "Common/LightCulling.slangh"
#include "Common/DebugDraw.slangh"

struct DebugLightPush
{
    DebugLightConstants constants;

    uint ID_Scene;
};

[[vk::push_constant]] ConstantBuffer< DebugLightPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Vertex count per light type; all even, so the two endpoints of a line are culled together
static const uint kSphereVertexCount = 192u;
static const uint kSpotVertexCount   = 96u;
static const uint kAreaVertexCount   = 138u;
static const uint kDiskVertexCount   = 162u;
static const uint kTubeVertexCount   = 136u;

static const uint kSphereSegments = 32u; // per great circle
static const uint kSpotRays       = 32u; // apex to rim
static const uint kSpotBaseSegs   = 16u;

struct LightSlot
{
    uint type;  // LIGHT_TYPE_*, 0 when the instance has no light
    uint index;
};

LightSlot DecodeLightSlot(uint instanceID, uint typeMask, LightCounts counts)
{
    const uint numSpots   = (typeMask & (1u << kDebugLightBitSpot))   != 0u ? counts.numSpots   : 0u;
    const uint numAreas   = (typeMask & (1u << kDebugLightBitArea))   != 0u ? counts.numAreas   : 0u;
    const uint numSpheres = (typeMask & (1u << kDebugLightBitSphere)) != 0u ? counts.numSpheres : 0u;
    const uint numDisks   = (typeMask & (1u << kDebugLightBitDisk))   != 0u ? counts.numDisks   : 0u;
    const uint numTubes   = (typeMask & (1u << kDebugLightBitTube))   != 0u ? counts.numTubes   : 0u;

    LightSlot slot = {};
    uint cursor = 0u;
    if (instanceID < cursor + numSpots)   { slot.type = LIGHT_TYPE_SPOT;   slot.index = instanceID - cursor; return slot; }
    cursor += numSpots;
    if (instanceID < cursor + numAreas)   { slot.type = LIGHT_TYPE_AREA;   slot.index = instanceID - cursor; return slot; }
    cursor += numAreas;
    if (instanceID < cursor + numSpheres) { slot.type = LIGHT_TYPE_SPHERE; slot.index = instanceID - cursor; return slot; }
    cursor += numSpheres;
    if (instanceID < cursor + numDisks)   { slot.type = LIGHT_TYPE_DISK;   slot.index = instanceID - cursor; return slot; }
    cursor += numDisks;
    if (instanceID < cursor + numTubes)   { slot.type = LIGHT_TYPE_TUBE;   slot.index = instanceID - cursor; return slot; }

    return slot;
}

void BuildBasis(float3 axis, out float3 t, out float3 b)
{
    const float3 seed = abs(axis.x) < 0.999 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
    t = normalize(cross(seed, axis));
    b = cross(axis, t);
}

float3 RingPoint(float3 center, float3 t, float3 b, float radius, float angle)
{
    return center + radius * (cos(angle) * t + sin(angle) * b);
}

// Full ring of `segs` lines: even vertex = segment start, odd vertex = segment end (wraps)
float3 RingVert(float3 center, float3 t, float3 b, float radius, uint vertexID, uint segs)
{
    const uint seg = vertexID >> 1;
    const uint k   = (seg + (vertexID & 1u)) % segs;
    const float angle = (float(k) / float(segs)) * 2.0 * PI;
    return RingPoint(center, t, b, radius, angle);
}

// Half ring from +e1 through +e2 to -e1 (angle 0..pi), no wrap
float3 HalfRingVert(float3 center, float3 e1, float3 e2, float radius, uint localV, uint segs)
{
    const uint seg = localV >> 1;
    const uint k   = seg + (localV & 1u);
    const float angle = (float(k) / float(segs)) * PI;
    return center + radius * (cos(angle) * e1 + sin(angle) * e2);
}

// Each *Vert function expects vertexID below its type's vertex count
float3 SphereVert(uint vertexID, float3 center, float radius)
{
    const uint circle = vertexID / (kSphereSegments * 2u);
    const uint localV = vertexID % (kSphereSegments * 2u);

    float3 t, b;
    if      (circle == 0u) { t = float3(1.0, 0.0, 0.0); b = float3(0.0, 1.0, 0.0); }
    else if (circle == 1u) { t = float3(0.0, 1.0, 0.0); b = float3(0.0, 0.0, 1.0); }
    else                   { t = float3(1.0, 0.0, 0.0); b = float3(0.0, 0.0, 1.0); }

    return RingVert(center, t, b, radius, localV, kSphereSegments);
}

float3 SpotVert(uint vertexID, float3 apex, float3 axis, float halfAngle, float height)
{
    float3 t, b;
    BuildBasis(axis, t, b);
    const float  rimRadius = height * tan(halfAngle);
    const float3 base      = apex + axis * height;

    if (vertexID < kSpotRays * 2u)
    {
        if ((vertexID & 1u) == 0u)
            return apex;

        const float angle = (float(vertexID >> 1) / float(kSpotRays)) * 2.0 * PI;
        return RingPoint(base, t, b, rimRadius, angle);
    }

    return RingVert(base, t, b, rimRadius, vertexID - kSpotRays * 2u, kSpotBaseSegs);
}

float3 AreaVert(uint vertexID, float3 center, float3 tangent, float3 normal, float halfW, float halfH, float rMax)
{
    const float3 bitangent = cross(normal, tangent);
    const float3 forward   = -normal; // normal is the back-face direction

    if (vertexID < 64u)
        return RingVert(center, tangent, bitangent, rMax, vertexID, 32u);
    if (vertexID < 96u)
        return HalfRingVert(center, tangent, forward, rMax, vertexID - 64u, 16u);
    if (vertexID < 128u)
        return HalfRingVert(center, bitangent, forward, rMax, vertexID - 96u, 16u);

    if (vertexID < 136u)
    {
        const uint edge = (vertexID - 128u) >> 1;
        const uint k    = (edge + (vertexID & 1u)) & 3u;
        const float signW = (k == 1u || k == 2u) ? 1.0 : -1.0;
        const float signH = (k >= 2u) ? 1.0 : -1.0;
        return center + tangent * (signW * halfW) + bitangent * (signH * halfH);
    }

    return vertexID == 136u ? center : center + forward * rMax * 0.4;
}

float3 DiskVert(uint vertexID, float3 center, float3 normal, float3 tangent, float diskRadius, float rMax)
{
    const float3 bitangent = cross(normal, tangent);
    const float3 forward   = -normal;

    if (vertexID < 64u)
        return RingVert(center, tangent, bitangent, rMax, vertexID, 32u);
    if (vertexID < 96u)
        return HalfRingVert(center, tangent, forward, rMax, vertexID - 64u, 16u);
    if (vertexID < 128u)
        return HalfRingVert(center, bitangent, forward, rMax, vertexID - 96u, 16u);
    if (vertexID < 160u)
        return RingVert(center, tangent, bitangent, diskRadius, vertexID - 128u, 16u);

    return vertexID == 160u ? center : center + forward * rMax * 0.4;
}

float3 TubeVert(uint vertexID, float3 a, float3 b, float rMax)
{
    const float3 axis = normalize(b - a);
    float3 t, n;
    BuildBasis(axis, t, n);

    if (vertexID < 32u)
        return HalfRingVert(a, t, -axis, rMax, vertexID, 16u);
    if (vertexID < 64u)
        return HalfRingVert(a, n, -axis, rMax, vertexID - 32u, 16u);
    if (vertexID < 96u)
        return HalfRingVert(b, t, axis, rMax, vertexID - 64u, 16u);
    if (vertexID < 128u)
        return HalfRingVert(b, n, axis, rMax, vertexID - 96u, 16u);

    const uint  localV = vertexID - 128u;
    const float angle  = (float(localV >> 1) / 4.0) * 2.0 * PI;
    const float3 side  = cos(angle) * t + sin(angle) * n;
    return ((localV & 1u) != 0u ? b : a) + side * rMax;
}

[shader("vertex")]
DebugVertexOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    DebugVertexOutput output;
    output.position = kCulledClipPosition;
    output.color    = float4(0.0);

    StructuredBuffer< SceneGlobals > scene = GetResource(g_Push.ID_Scene);
    ByteAddressBuffer lights = GetResource(scene[0].buffers.lights);

    const LightSlot slot = DecodeLightSlot(instanceID, g_Push.constants.typeMask, LoadLightCounts(lights));

    float3 positionWS;
    if (slot.type == LIGHT_TYPE_SPHERE && vertexID < kSphereVertexCount)
    {
        const SphereLight light = LoadSphereLight(lights, slot.index);
        positionWS = SphereVert(vertexID, light.position, InfluenceRadiusIsotropic(light.luminousFluxLm, light.radius));
    }
    else if (slot.type == LIGHT_TYPE_SPOT && vertexID < kSpotVertexCount)
    {
        const SpotLight light = LoadSpotLight(lights, slot.index);
        const float height = InfluenceRadiusCone(light.luminousFluxLm, light.outerConeAngleRad, light.radiusM);
        positionWS = SpotVert(vertexID, light.position, normalize(light.direction), light.outerConeAngleRad, height);
    }
    else if (slot.type == LIGHT_TYPE_AREA && vertexID < kAreaVertexCount)
    {
        const AreaLight light = LoadAreaLight(lights, slot.index);
        // Same influence radius as light culling uses for area lights
        const float rPhysical = sqrt(light.halfWidth * light.halfWidth + light.halfHeight * light.halfHeight);
        const float rMax      = InfluenceRadiusIsotropic(light.luminousFluxLm, rPhysical);
        positionWS = AreaVert(vertexID, light.position, normalize(light.tangent), normalize(light.normal), light.halfWidth, light.halfHeight, rMax);
    }
    else if (slot.type == LIGHT_TYPE_DISK && vertexID < kDiskVertexCount)
    {
        const DiskLight light = LoadDiskLight(lights, slot.index);
        const float rMax = InfluenceRadiusIsotropic(light.luminousFluxLm, light.radius);
        positionWS = DiskVert(vertexID, light.position, normalize(light.normal), normalize(light.tangent), light.radius, rMax);
    }
    else if (slot.type == LIGHT_TYPE_TUBE && vertexID < kTubeVertexCount)
    {
        const TubeLight light = LoadTubeLight(lights, slot.index);
        positionWS = TubeVert(vertexID, light.positionA, light.positionB, InfluenceRadiusIsotropic(light.luminousFluxLm, light.radius));
    }
    else
    {
        return output;
    }

    // Light gizmos follow the live lights in world space, not the frozen view
    output.position = mul(scene[0].camera.mViewProj, float4(positionWS, 1.0));
    output.color    = float4(g_Push.constants.lineColor, g_Push.constants.lineAlpha);
    return output;
}
