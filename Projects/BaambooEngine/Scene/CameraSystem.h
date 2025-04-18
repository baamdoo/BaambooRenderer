#pragma once
#include "Components.h"

namespace baamboo
{

class CameraSystem
{
public:
	CameraSystem(entt::registry& registry);

	void OnCameraConstructed(entt::registry& registry, entt::entity entity);
	void OnCameraUpdated(entt::registry& registry, entt::entity entity);
	void OnCameraDestroy(entt::registry& registry, entt::entity entity);

	void Update();

	[[nodiscard]]
	const mat4& ViewMatrix() const { return m_mView; }
	[[nodiscard]]
	const mat4& ProjMatrix() const { return m_mProj; }
	[[nodiscard]]
	mat4 ViewProjMatrix() const { return m_mProj * m_mView; }

private:
	entt::registry& m_registry;

	mat4 m_mProj;
	mat4 m_mView;
};

} // namespace baamboo