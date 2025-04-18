#include "BaambooPch.h"
#include "CameraSystem.h"

namespace baamboo
{

CameraSystem::CameraSystem(entt::registry& registry)
	: m_registry(registry)
{
	m_registry.on_construct< CameraComponent >().connect< &CameraSystem::OnCameraConstructed >(this);
	m_registry.on_update< CameraComponent >().connect< &CameraSystem::OnCameraUpdated >(this);
	m_registry.on_destroy< CameraComponent >().connect< &CameraSystem::OnCameraUpdated >(this);
}

void CameraSystem::OnCameraConstructed(entt::registry& registry, entt::entity entity)
{
	// set default value of camera-component
	auto& camera = registry.get< CameraComponent >(entity);
	camera.type = CameraComponent::eType::Perspective;
	camera.cNear = 0.1f;
	camera.cFar = 1000.0f;
	camera.fov = 45.0f;
}

void CameraSystem::OnCameraUpdated(entt::registry& registry, entt::entity entity)
{
	// adjust undetermined values
	auto& camera = registry.get< CameraComponent >(entity);

	camera.cNear = std::max(camera.cNear, 0.01f);
	camera.cNear = std::min(camera.cNear, 1.0f);
	camera.cFar = std::max(camera.cFar, 10.0f);
	camera.cFar = std::min(camera.cFar, 10000.0f);
	camera.bDirtyMark = true;
}

void CameraSystem::OnCameraDestroy(entt::registry& registry, entt::entity entity)
{
}

void CameraSystem::Update()
{
	m_registry.view< CameraComponent >().each([this](auto entity, auto& cameraComponent)
		{
			if (cameraComponent.bDirtyMark)
			{
				// TODO
				cameraComponent.bDirtyMark = false;
			}
		});
}

} // namespace baamboo