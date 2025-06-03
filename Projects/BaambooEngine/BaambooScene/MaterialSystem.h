#pragma once
#include "Components.h"

namespace baamboo
{

class MaterialSystem
{
public:
	MaterialSystem(entt::registry& registry);

	void OnMaterialConstructed(entt::registry& registry, entt::entity entity);
	void OnMaterialUpdated(entt::registry& registry, entt::entity entity);
	void OnMaterialDestroyed(entt::registry& registry, entt::entity entity);

	std::vector< entt::entity > Update();

private:
	entt::registry& m_Registry;
};

} // namespace baamboo