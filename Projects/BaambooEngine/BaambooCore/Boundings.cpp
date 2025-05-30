#include "BaambooPch.h"
#include "Boundings.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

//-------------------------------------------------------------------------
// Bounding sphere
//-------------------------------------------------------------------------
BoundingSphere::BoundingSphere(const BoundingBox& aabb)
{
	m_Center = (aabb.Min() + aabb.Max()) * 0.5f;
	m_Radius = glm::distance(m_Center, aabb.Max());
}

BoundingSphere& BoundingSphere::operator*(const mat4& transform)
{
	float3 scale, translation, skew;
	float4 perspective;
	glm::quat rotation;
	glm::decompose(transform, scale, rotation, translation, skew, perspective);

	m_Center = float3(glm::translate(mat4(1.f), translation) * float4(m_Center, 1.f));
	m_Radius = std::max({ scale.x, scale.y, scale.z });

	return *this;
}

bool BoundingSphere::Surrounds(const float3& point) const
{
	const auto distance = glm::distance(m_Center, point);
	return distance <= m_Radius;
}

bool BoundingSphere::Surrounds(const BoundingBox& aabb) const
{
	return Surrounds(BoundingSphere(aabb));
}

bool BoundingSphere::Surrounds(const BoundingSphere& other) const
{
	const float distance = glm::distance(m_Center, other.Center());
	return distance + other.Radius() <= m_Radius;
}

bool BoundingSphere::Overlaps(const BoundingBox& aabb) const
{
	return Overlaps(BoundingSphere(aabb));
}

bool BoundingSphere::Overlaps(const BoundingSphere& other) const
{
	const float distance = glm::distance(m_Center, other.Center());
	return distance < m_Radius + other.Radius();
}

const BoundingSphere BoundingSphere::Union(const BoundingSphere& sphere, const float3& point)
{
	if (sphere.Surrounds(point))
		return sphere;

	float3 direction = glm::normalize(point - sphere.Center());
	float3 farthestPoint = sphere.Center() - direction * sphere.Radius();

	float3 newCenter = (farthestPoint + point) * 0.5f;
	float newRadius = glm::distance(farthestPoint, point) * 0.5f;

	return BoundingSphere(newCenter, newRadius);
}

const BoundingSphere BoundingSphere::Union(const BoundingSphere& sphere1, const BoundingSphere& sphere2)
{
	const float distance = glm::distance(sphere1.Center(), sphere2.Center());
	if (distance + sphere2.Radius() <= sphere1.Radius())
		return sphere1;
	if (distance + sphere1.Radius() <= sphere2.Radius())
		return sphere2;

	float3 direction = glm::normalize(sphere2.Center() - sphere1.Center());
	float3 farthestPointSphere1 = sphere1.Center() - direction * sphere1.Radius();
	float3 farthestPointSphere2 = sphere2.Center() + direction * sphere2.Radius();

	float3 newCenter = (farthestPointSphere1 + farthestPointSphere2) * 0.5f;
	float newRadius = glm::distance(farthestPointSphere1, farthestPointSphere2) * 0.5f;

	return BoundingSphere(newCenter, newRadius);
}


//-------------------------------------------------------------------------
// BoundingBox
//-------------------------------------------------------------------------
BoundingBox::BoundingBox(const BoundingSphere& sphere)
{
	const auto center = sphere.Center();
	const auto r = sphere.Radius();
	const float3 radius = float3(r, r, r);
	m_Min = center - radius;
	m_Max = center + radius;
}

bool BoundingBox::Surrounds(const float3& point) const
{
	if (!glm::lessThan(point, m_Min).length()) return false;
	if (!glm::greaterThan(point, m_Max).length()) return false;
	return true;
}

bool BoundingBox::Surrounds(const BoundingBox& other) const
{
	return Surrounds(other.Min()) && Surrounds(other.Max());
}

bool BoundingBox::Surrounds(const BoundingSphere& sphere) const
{
	return Surrounds(BoundingBox(sphere));
}

bool BoundingBox::Overlaps(const BoundingBox& other) const
{
	if (m_Max.x < other.Min().x || other.Max().x < m_Min.x) return false;
	if (m_Max.y < other.Min().y || other.Max().y < m_Min.y) return false;
	if (m_Max.z < other.Min().z || other.Max().z < m_Min.z) return false;
	return true;
}

bool BoundingBox::Overlaps(const BoundingSphere& sphere) const
{
	return Overlaps(BoundingBox(sphere));
}

const BoundingBox BoundingBox::Union(const BoundingBox& aabb, const float3& point)
{
	return Union(aabb, BoundingBox(point));
}

const BoundingBox BoundingBox::Union(const BoundingBox& aabb1, const BoundingBox& aabb2)
{
	const auto min = glm::min(aabb1.m_Min, aabb2.m_Min);
	const auto max = glm::max(aabb1.m_Max, aabb2.m_Max);
	return BoundingBox(min, max);
}