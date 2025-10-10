#pragma once
#include "Defines.h"
#include "MathTypes.h"

//-------------------------------------------------------------------------
// Bounding Sphere
//-------------------------------------------------------------------------
class BAAMBOO_API BoundingSphere
{
public:
	BoundingSphere() : m_Center(0.f, 0.f, 0.f), m_Radius(1.f) {}
	BoundingSphere(float3 center, f32 radius) : m_Center(center), m_Radius(radius) {}
	BoundingSphere(const BoundingSphere&) = default;
	BoundingSphere(BoundingSphere&&) = default;
	BoundingSphere(const class BoundingBox& aabb);
	~BoundingSphere() = default;

	BoundingSphere& operator=(const BoundingSphere&) = default;
	BoundingSphere& operator=(BoundingSphere&&) = default;
	BoundingSphere& operator*(const mat4& transform);

	[[nodiscard]]
	bool Surrounds(const float3& point) const;
	[[nodiscard]]
	bool Surrounds(const class BoundingBox& aabb) const;
	[[nodiscard]]
	bool Surrounds(const class BoundingSphere& other) const;

	[[nodiscard]]
	bool Overlaps(const class BoundingBox& aabb) const;
	[[nodiscard]]
	bool Overlaps(const class BoundingSphere& other) const;

public:
	[[nodiscard]]
	float3 Center() const { return m_Center; }
	[[nodiscard]]
	f32 Radius() const { return m_Radius; }

	[[nodiscard]]
	static const BoundingSphere Union(const BoundingSphere& sphere, const float3& point);
	[[nodiscard]]
	static const BoundingSphere Union(const BoundingSphere& sphere1, const BoundingSphere& sphere2);

private:
	float3 m_Center;
	f32    m_Radius;
};


//-------------------------------------------------------------------------
// BoundingBox
//-------------------------------------------------------------------------
class BAAMBOO_API BoundingBox
{
public:
	BoundingBox() = default;
	BoundingBox(float3 point) : m_Min(point), m_Max(point) {}
	BoundingBox(float3 min, float3 max) : m_Min(min), m_Max(max) {}
	BoundingBox(const BoundingBox&) = default;
	BoundingBox(BoundingBox&&) = default;
	explicit BoundingBox(const BoundingSphere& sphere);
	~BoundingBox() = default;

	BoundingBox& operator=(const BoundingBox&) = default;
	BoundingBox& operator=(BoundingBox&&) = default;

	[[nodiscard]]
	bool Surrounds(const float3& point) const;
	[[nodiscard]]
	bool Surrounds(const class BoundingBox& other) const;
	[[nodiscard]]
	bool Surrounds(const class BoundingSphere& sphere) const;

	[[nodiscard]]
	bool Overlaps(const class BoundingBox& other) const;
	[[nodiscard]]
	bool Overlaps(const class BoundingSphere& sphere) const;

public:
	[[nodiscard]]
	float3 Min() const { return m_Min; }
	[[nodiscard]]
	float3 Max() const { return m_Max; }

	[[nodiscard]]
	static const BoundingBox Union(const BoundingBox& aabb, const float3& point);
	[[nodiscard]]
	static const BoundingBox Union(const BoundingBox& aabb1, const BoundingBox& aabb2);

private:
	float3 m_Min;
	float3 m_Max;
};