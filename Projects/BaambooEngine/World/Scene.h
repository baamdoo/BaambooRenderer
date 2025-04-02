#pragma once
#include "Components.h"

namespace baamboo
{

constexpr size_t NUM_MAX_ENTITIES = 8 * 1024 * 1024;

class Scene
{
public:
	Scene(const std::string& name);
	~Scene() = default;

	class Entity CreateEntity(const std::string& tag);
	void RemoveEntity(Entity entity);

	[[nodiscard]]
	const std::string& Name() const { return m_name; }
	[[nodiscard]]
	bool IsLoading() const { return m_bLoading; }

private:
	void SortEntities();

private:
	friend class Entity;
	entt::registry m_registry;

	std::string m_name;
	bool m_bLoading = false;
};

} // namespace baamboo 