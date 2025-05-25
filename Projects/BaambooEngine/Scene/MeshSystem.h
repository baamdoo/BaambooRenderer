#pragma once
#include "Components.h"

namespace baamboo
{

class StaticMeshSystem
{
public:
	StaticMeshSystem(entt::registry& registry);

	void OnMeshConstructed(entt::registry& registry, entt::entity entity);
	void OnMeshUpdated(entt::registry& registry, entt::entity entity);
	void OnMeshDestroyed(entt::registry& registry, entt::entity entity);

	std::vector< entt::entity > Update();

private:
	entt::registry& m_registry;
};

} // namespace baamboo