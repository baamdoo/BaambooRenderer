#pragma once
#include "MathTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace baamboo
{

namespace SDF
{

// World/primitive-space meters squared. Capsule segments shorter than this behave as spheres.
constexpr float kCapsuleDegenerateSegmentEpsilonMeter = 1.0e-5f;
constexpr float kCapsuleDegenerateSegmentEpsilonSquaredMeter =
    kCapsuleDegenerateSegmentEpsilonMeter * kCapsuleDegenerateSegmentEpsilonMeter;

inline float QuietNaN()
{
    return std::numeric_limits<float>::quiet_NaN();
}

inline float3 QuietNaN3()
{
    const float nan = QuietNaN();
    return float3(nan, nan, nan);
}

// p and primitive parameters must be expressed in the same coordinate space.
inline float Sphere(const float3& p, const float3& center, float radius)
{
    return glm::length(p - center) - radius;
}

// p, center, and halfExtent must be expressed in the same coordinate space.
inline float AxisAlignedBox(const float3& p, const float3& center, const float3& halfExtent)
{
    const float3 q = glm::abs(p - center) - halfExtent;

    const float dOut = glm::length(glm::max(q, float3(0.0f)));
    const float dIn = std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
    return dOut + dIn;
}

// p, segmentA, segmentB, and radius must be expressed in the same coordinate space.
inline float Capsule(const float3& p, const float3& segmentA, const float3& segmentB, float radius)
{
    const float3 pa = p - segmentA;
    const float3 ba = segmentB - segmentA;
    const float  baLengthSquared = glm::dot(ba, ba);
    if (baLengthSquared <= kCapsuleDegenerateSegmentEpsilonSquaredMeter)
        return Sphere(p, segmentA, radius);

    const float  t = glm::clamp(glm::dot(pa, ba) / baLengthSquared, 0.0f, 1.0f);
    const float3 c = segmentA + t * ba;
    return glm::length(p - c) - radius;
}

inline float Union(float a, float b)
{
    return std::min(a, b);
}

inline float Intersection(float a, float b)
{
    return std::max(a, b);
}

inline float Subtract(float a, float b)
{
    return std::max(a, -b);
}

// Transform order is scale, rotation, translation. This evaluates the inverse transform into primitive space.
inline float3 InverseTransformPointUniformScale(
    const float3& p,
    const float3& translation,
    const quat& rotationToParent,
    float uniformScale)
{
    if (!(uniformScale > 0.0f))
        return QuietNaN3();

    const float3 unrotated = glm::inverse(rotationToParent) * (p - translation);
    return unrotated / uniformScale;
}

// Primitive-space exact Euclidean SDFs must be multiplied by the positive uniform scale after inverse-transforming p.
inline float ApplyUniformScaleToDistance(float localDistance, float uniformScale)
{
    if (!(uniformScale > 0.0f))
        return QuietNaN();

    return localDistance * uniformScale;
}

// Transform order is non-uniform scale, rotation, translation. Resulting fields are DistanceLike.
inline float3 InverseTransformPointNonUniformScale(
    const float3& p,
    const float3& translation,
    const quat& rotationToParent,
    const float3& nonUniformScale)
{
    constexpr float kScaleEpsilon = 1.0e-6f;
    if (std::abs(nonUniformScale.x) <= kScaleEpsilon ||
        std::abs(nonUniformScale.y) <= kScaleEpsilon ||
        std::abs(nonUniformScale.z) <= kScaleEpsilon)
    {
        return QuietNaN3();
    }

    const float3 unrotated = glm::inverse(rotationToParent) * (p - translation);
    return unrotated / nonUniformScale;
}

} // namespace SDF

} // namespace baamboo
