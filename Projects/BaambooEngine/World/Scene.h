#pragma once
#include "Components.h"

namespace baamboo
{

constexpr size_t NUM_MAX_ENTITIES = 8 * 1024 * 1024;

class TransformSystem;

class Scene
{
public:
	Scene(const std::string& name);
	~Scene();

	[[nodiscard]]
	class Entity CreateEntity(const std::string& tag = "Empty");
	void RemoveEntity(Entity entity);

	void Update(float dt);

	[[nodiscard]]
	const std::string& Name() const { return m_name; }
	[[nodiscard]]
	bool IsLoading() const { return m_bLoading; }

	[[nodiscard]]
	const entt::registry& Registry() const { return m_registry; }
	[[nodiscard]]
	TransformSystem* GetTransformSystem() const { return m_pTransformSystem; }

private:
	friend class Entity;
	entt::registry m_registry;

	std::string m_name;
	bool m_bLoading = false;

	TransformSystem* m_pTransformSystem;
};

} // namespace baamboo 