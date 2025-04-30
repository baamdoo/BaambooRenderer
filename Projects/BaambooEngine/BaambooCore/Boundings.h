#pragma once

//-------------------------------------------------------------------------
// Bounding Sphere
//-------------------------------------------------------------------------
class BoundingSphere
{
public:
	BoundingSphere() : m_center(0.f, 0.f, 0.f), m_radius(1.f) {}
	BoundingSphere(float3 center, f32 radius) : m_center(center), m_radius(radius) {}
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
	float3 Center() const { return m_center; }
	[[nodiscard]]
	f32 Radius() const { return m_radius; }

	[[nodiscard]]
	static const BoundingSphere Union(const BoundingSphere& sphere, const float3& point);
	[[nodiscard]]
	static const BoundingSphere Union(const BoundingSphere& sphere1, const BoundingSphere& sphere2);

private:
	float3 m_center;
	f32    m_radius;
};


//-------------------------------------------------------------------------
// BoundingBox
//-------------------------------------------------------------------------
class BoundingBox
{
public:
	BoundingBox() = default;
	BoundingBox(float3 point) : m_min(point), m_max(point) {}
	BoundingBox(float3 min, float3 max) : m_min(min), m_max(max) {}
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
	float3 Min() const { return m_min; }
	[[nodiscard]]
	float3 Max() const { return m_max; }

	[[nodiscard]]
	static const BoundingBox Union(const BoundingBox& aabb, const float3& point);
	[[nodiscard]]
	static const BoundingBox Union(const BoundingBox& aabb1, const BoundingBox& aabb2);

private:
	float3 m_min;
	int    padding0;
	float3 m_max;
	int    padding1;
};