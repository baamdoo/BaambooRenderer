#include "BaambooPch.h"
#include "Camera.h"
#include "BaambooCore/Input.hpp"
#include "BaambooUtils/Math.hpp"

namespace baamboo
{

void CameraController_FirstPerson::Update(f32 dt)
{
	// **
	// rotation
	// **
	if (Input::Inst()->IsMouseDown(GLFW_MOUSE_BUTTON_LEFT))
	{
		m_rotationVelocity = float3(0.0f);
	}
	else if (Input::Inst()->IsMousePressed(GLFW_MOUSE_BUTTON_LEFT))
	{
		float3 rotationImpulse = float3(Input::Inst()->GetMouseDeltaX(), Input::Inst()->GetMouseDeltaY(), 0.0f);
		m_rotationVelocity += rotationImpulse * config.rotationAcceleration * dt;
	}
	m_rotationVelocity += -m_rotationVelocity * std::clamp(config.rotationDamping * dt, 0.0f, 0.75f);

	m_rotationVelocity.x = std::clamp(m_rotationVelocity.x, -config.maxRotationSpeed, config.maxRotationSpeed);
	m_rotationVelocity.y = std::clamp(m_rotationVelocity.y, -config.maxRotationSpeed, config.maxRotationSpeed);
	m_rotationVelocity.z = std::clamp(m_rotationVelocity.z, -config.maxRotationSpeed, config.maxRotationSpeed);

	m_transform.Rotate(m_rotationVelocity.x * dt, m_rotationVelocity.y * dt, 0);


	// **
	// movement
	// **
	float3 impulseLOCAL = 
	{
		static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_D)) - static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_A)),
		0.0f,
		static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_W)) - static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_S))
	};

	float3 impulseWORLD = m_transform.Orientation() * impulseLOCAL;
	impulseWORLD += 
		float3(0.0f, static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_Q)) - static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_E)), 0.0f);

	m_moveVelocity += impulseWORLD * config.moveAcceleration * config.movementScale * dt *
		(Input::Inst()->IsKeyPressed(GLFW_KEY_LEFT_CONTROL) ? config.boostingSpeed : 1.0f);
	m_moveVelocity -= m_moveVelocity * std::clamp(config.moveDamping * dt, 0.0f, 0.75f);

	const float maxSpeed = 
		Input::Inst()->IsKeyPressed(GLFW_KEY_LEFT_CONTROL) ? config.maxMoveSpeed * config.boostingSpeed : config.maxMoveSpeed;
	if (m_moveVelocity.length() > maxSpeed * config.movementScale)
		m_moveVelocity = glm::normalize(m_moveVelocity) * maxSpeed;

	if (m_moveVelocity.length() < 1e-3)
	{
		m_moveVelocity = glm::zero< float3 >();
	}

	m_transform.position += m_moveVelocity * dt;
}

} // namespace baamboo