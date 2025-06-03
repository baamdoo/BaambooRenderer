#include "BaambooPch.h"
#include "Camera.h"
#include "BaambooCore/Input.hpp"
#include "Utils/Math.hpp"

namespace baamboo
{

void CameraController_FirstPerson::Update(f32 dt)
{
	// **
	// rotation
	// **
	if (Input::Inst()->IsMouseDown(GLFW_MOUSE_BUTTON_LEFT))
	{
		m_RotationVelocity = float3(0.0f);
	}
	else if (Input::Inst()->IsMousePressed(GLFW_MOUSE_BUTTON_LEFT))
	{
		float3 rotationImpulse = float3(Input::Inst()->GetMouseDeltaX(), Input::Inst()->GetMouseDeltaY(), 0.0f);
		m_RotationVelocity += rotationImpulse * config.rotationAcceleration * dt;
	}
	m_RotationVelocity += -m_RotationVelocity * std::clamp(config.rotationDamping * dt, 0.0f, 0.75f);

	m_RotationVelocity.x = std::clamp(m_RotationVelocity.x, -config.maxRotationSpeed, config.maxRotationSpeed);
	m_RotationVelocity.y = std::clamp(m_RotationVelocity.y, -config.maxRotationSpeed, config.maxRotationSpeed);
	m_RotationVelocity.z = std::clamp(m_RotationVelocity.z, -config.maxRotationSpeed, config.maxRotationSpeed);

	m_Transform.Rotate(m_RotationVelocity.x * dt, m_RotationVelocity.y * dt, 0);


	// **
	// movement
	// **
	float3 impulseLOCAL = 
	{
		static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_D)) - static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_A)),
		0.0f,
		static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_W)) - static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_S))
	};

	float3 impulseWORLD = m_Transform.Orientation() * impulseLOCAL;
	impulseWORLD += 
		float3(0.0f, static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_Q)) - static_cast<float>(Input::Inst()->IsKeyPressed(GLFW_KEY_E)), 0.0f);

	m_MoveVelocity += impulseWORLD * config.moveAcceleration * config.movementScale * dt *
		(Input::Inst()->IsKeyPressed(GLFW_KEY_LEFT_CONTROL) ? config.boostingSpeed : 1.0f);
	m_MoveVelocity -= m_MoveVelocity * std::clamp(config.moveDamping * dt, 0.0f, 0.75f);

	const float maxSpeed = 
		Input::Inst()->IsKeyPressed(GLFW_KEY_LEFT_CONTROL) ? config.maxMoveSpeed * config.boostingSpeed : config.maxMoveSpeed;
	if (m_MoveVelocity.length() > maxSpeed * config.movementScale)
		m_MoveVelocity = glm::normalize(m_MoveVelocity) * maxSpeed;

	if (m_MoveVelocity.length() < 1e-3)
	{
		m_MoveVelocity = glm::zero< float3 >();
	}

	m_Transform.position += m_MoveVelocity * dt;
}

} // namespace baamboo