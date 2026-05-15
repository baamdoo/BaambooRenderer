#ifndef _HLSL_LIGHT_CULLING_COMMON_HEADER
#define _HLSL_LIGHT_CULLING_COMMON_HEADER
#include "../Common.bsh"


void DecodeClusterAABB(ClusterAABB raw, out float3 aabbMin, out float3 aabbMax)
{
    aabbMin = float3(raw.minX, raw.minY, raw.minZ);
    aabbMax = float3(raw.maxX, raw.maxY, raw.maxZ);
}

uint ClusterFlatIndex(uint3 tid, uint numTilesX, uint numTilesY)
{
    return (tid.z * numTilesY + tid.y) * numTilesX + tid.x;
}

// -------------------------------------------------------------------------
// Light index encoding — 32-bit packed (type:3 + idx:29)
// -------------------------------------------------------------------------
uint EncodeLightIndex(uint type, uint idx)
{
    return (type << LIGHT_INDEX_BITS) | (idx & LIGHT_INDEX_MASK);
}

void DecodeLightIndex(uint packed, out uint type, out uint idx)
{
    type = packed >> LIGHT_INDEX_BITS;
    idx  = packed & LIGHT_INDEX_MASK;
}

// -------------------------------------------------------------------------
// Influence radius
// Reference: https://blog.selfshadow.com/publications/s2014-shading-course/ [Moving Frostbite to PBR]
// -------------------------------------------------------------------------
float InfluenceRadiusIsotropic(float fluxLm, float physRadius)
{
    return max(physRadius, sqrt(fluxLm / (4.0 * PI * LIGHT_INFLUENCE_THRESHOLD)));
}

float InfluenceRadiusCone(float fluxLm, float outerConeAngleRad, float physRadius)
{
    if (outerConeAngleRad <= 0.0 || fluxLm <= 0.0)
        return 0.0;
    float solidAngle = 2.0 * PI * (1.0 - cos(outerConeAngleRad));
    return max(physRadius, sqrt(fluxLm / max(solidAngle * LIGHT_INFLUENCE_THRESHOLD, 1e-6)));
}

float SmoothDistanceAtt(float distSq, float invRMaxSq)
{
    float factor = distSq * invRMaxSq;
    float smooth = saturate(1.0 - factor * factor);
    return smooth * smooth;
}

bool IntersectAabbSphere(float3 aabbMin, float3 aabbMax, float3 sphereCenter, float sphereRadius)
{
    float3 closest = clamp(sphereCenter, aabbMin, aabbMax);
    float3 diff    = closest - sphereCenter;
    return dot(diff, diff) <= sphereRadius * sphereRadius;
}

bool IntersectAabbCapsule(float3 aabbMin, float3 aabbMax, float3 segA, float3 segB, float capRadius)
{
    const int K = 8;

    float minDistSq = FLT_MAX;
    [unroll]
    for (int k = 0; k <= K; ++k)
    {
        float  t = float(k) / float(K);
        float3 q  = lerp(segA, segB, t); // segment point

        float3 closest = clamp(q, aabbMin, aabbMax);
        float3 diff    = closest - q;

        minDistSq = min(minDistSq, dot(diff, diff));
    }

    return minDistSq <= capRadius * capRadius;
}

bool IntersectAabbCone(float3 aabbMin, float3 aabbMax, float3 apex, float3 axisDir, float range, float cosHalfAngle)
{
    // Step 1 — fast reject via apex-centered bounding sphere
    float coneBoundRadius = range / max(cosHalfAngle, 1e-6);
    if (!IntersectAabbSphere(aabbMin, aabbMax, apex, coneBoundRadius))
        return false;

    float3 aabbCenter        = (aabbMin + aabbMax) * 0.5;
    float3 aabbExtent        = (aabbMax - aabbMin) * 0.5;
    float  signedDist        = dot(axisDir, aabbCenter - apex);
    float  aabbExtAlongAxis  = dot(aabbExtent, abs(axisDir));

    // Step 2 — entire AABB behind apex (cone backward)
    if (signedDist + aabbExtAlongAxis < 0.0)
        return false;

    // Step 3 — entire AABB beyond range
    if (signedDist - aabbExtAlongAxis > range)
        return false;

    // Step 4 — cone angle test
    float3 perpVec    = (aabbCenter - apex) - axisDir * signedDist;
    float  perpDist   = length(perpVec);
    float  sinHalf    = sqrt(max(1.0 - cosHalfAngle * cosHalfAngle, 0.0));
    float  tanHalf    = sinHalf / max(cosHalfAngle, 1e-6);
    float  effDist    = max(signedDist, 0.0);
    float  coneRadius = effDist * tanHalf;
    float  extentPerp = length(aabbExtent);

    return perpDist <= coneRadius + extentPerp;
}

bool IntersectAabbHemisphere(float3 aabbMin, float3 aabbMax, float3 center, float3 backNormal, float radius)
{
    // Step 1 — sphere fast reject
    if (!IntersectAabbSphere(aabbMin, aabbMax, center, radius))
        return false;

    // Step 2 — front-most vertex extreme test
    float3 frontVertex = float3(
        backNormal.x < 0.0 ? aabbMax.x : aabbMin.x,
        backNormal.y < 0.0 ? aabbMax.y : aabbMin.y,
        backNormal.z < 0.0 ? aabbMax.z : aabbMin.z);
    return dot(backNormal, frontVertex - center) <= 0.0;
}

bool IntersectAabbDisk(float3 aabbMin, float3 aabbMax, float3 diskCenter, float3 diskNormal, float diskRadius)
{
    return IntersectAabbHemisphere(aabbMin, aabbMax, diskCenter, diskNormal, diskRadius);
}

uint PixelToClusterIdx(uint2 pixelXY, float viewZ, float zNear, float zFar, uint screenW, uint screenH)
{
    uint tx = pixelXY.x / CLUSTER_TILE_SIZE_PX;
    uint ty = pixelXY.y / CLUSTER_TILE_SIZE_PX;

    float k  = float(CLUSTER_SLICES_Z) * log(viewZ / zNear) / log(zFar / zNear);
    uint  tz = uint(clamp(k, 0.0, float(CLUSTER_SLICES_Z - 1)));

    uint numTilesX = (screenW + CLUSTER_TILE_SIZE_PX - 1) / CLUSTER_TILE_SIZE_PX;
    uint numTilesY = (screenH + CLUSTER_TILE_SIZE_PX - 1) / CLUSTER_TILE_SIZE_PX;

    return ClusterFlatIndex(uint3(tx, ty, tz), numTilesX, numTilesY);
}

#endif // _HLSL_LIGHT_CULLING_COMMON_HEADER
