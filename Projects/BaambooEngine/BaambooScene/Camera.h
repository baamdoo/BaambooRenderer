#pragma once
#include "Transform.hpp"

namespace baamboo
{

class CameraController
{
public:
	virtual ~CameraController() = default;
	virtual mat4 GetView() const = 0;
	virtual float3 GetPosition() const = 0;

	virtual Transform& GetTransform() { return m_Transform; }

protected:
	Transform m_Transform = {};
};

class EditorCamera final
{
public:
	explicit EditorCamera(CameraController& controller, u32 width, u32 height)
		: m_Controller(controller)
		, m_ViewportWidth(width)
		, m_ViewportHeight(height)
	{
	}

	EditorCamera(const EditorCamera&) = default;
	EditorCamera& operator=(const EditorCamera&) = default;

	[[nodiscard]]
	mat4 GetView() const { return m_Controller.GetView(); }
	[[nodiscard]]
	mat4 GetProj() const 
	{ 
		return m_Type == eType::Perspective ?
			infinitePerspectiveFovReverseZLH_ZO(glm::radians(fov), (float)m_ViewportWidth, (float)m_ViewportHeight, zNear) :
			glm::orthoLH_ZO(0.0f, (float)m_ViewportWidth, 0.0f, (float)m_ViewportHeight, zNear, zFar);
	}
	[[nodiscard]]
	mat4 GetProj(float zFar_) const
	{
		return m_Type == eType::Perspective ?
			perspectiveFovReverseZLH_ZO(glm::radians(fov), (float)m_ViewportWidth, (float)m_ViewportHeight, zNear, zFar_) :
			glm::orthoLH_ZO(0.0f, (float)m_ViewportWidth, 0.0f, (float)m_ViewportHeight, zNear, zFar);
	}
	[[nodiscard]]
	float3 GetPosition() const { return m_Controller.GetPosition(); }
	[[nodiscard]]
	bool IsPerspective() const { return m_Type == eType::Perspective; }
	[[nodiscard]]
	float GetAspectRatio() const { return float(m_ViewportWidth) / float(m_ViewportHeight); }

	float zNear = 0.1f;
	float zFar  = 1000.0f;
	float fov   = 45.0f;

	float maxVisibleDistance = 10'000.0f; // 10km

private:
	CameraController& m_Controller;

	enum class eType { Orthographic, Perspective } m_Type = eType::Perspective;

	u32 m_ViewportWidth;
	u32 m_ViewportHeight;
};

class CameraController_FirstPerson final : public CameraController
{
public:
	CameraController_FirstPerson() = default;
	CameraController_FirstPerson(const float3& pos, const float3& target)
	{
		SetLookAt(pos, target);
	}

	void Update(f32 dt);
	void Reset();

	[[nodiscard]]
	virtual mat4 GetView() const override { return glm::inverse(m_Transform.Matrix()); }
	[[nodiscard]]
	virtual float3 GetPosition() const override { return m_Transform.position; }

	void SetLookAt(const float3& pos, const float3& target)
	{
		SetLookAt(pos, target, float3(0.0f, 1.0f, 0.0f));
	}

	void SetLookAt(const float3& pos, const float3& target, const float3& worldUp)
	{
		m_Transform.position = pos;

		float3 forward = target - pos;
		const float fLen = glm::length(forward);
		if (fLen < 1e-6f)
		{
			m_Transform.SetOrientation(mat4(1.0f));
			return;
		}
		forward /= fLen;

		float3 up = glm::length(worldUp) > 1.0e-6f ? glm::normalize(worldUp) : float3(0.0f, 1.0f, 0.0f);
		if (std::abs(glm::dot(forward, up)) > 0.9999f)
			up = std::abs(forward.y) < 0.9999f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);

		m_Transform.SetOrientation(glm::transpose(glm::lookAtLH(float3(0.0f), forward, up)));
	}

	struct
	{
		float rotationAcceleration = 100.0f;
		float rotationDamping      = 5.0f;
		float maxRotationSpeed     = 1.0f;

		float moveAcceleration = 100.0f;
		float moveDamping      = 5.0f;
		float maxMoveSpeed     = 10.0f;
		float movementScale    = 1.0f; // cm
		float boostingSpeed    = 10.0f;
	} config;

private:
	float3 m_MoveVelocity     = float3(0.0f);
	float3 m_RotationVelocity = float3(0.0f);
};

} // namespace baamboo