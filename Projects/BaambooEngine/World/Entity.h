#pragma once
#include "Scene.h"

namespace baamboo
{

class Entity
{
public:
	using id_type = ENTT_ID_TYPE;

	Entity() = default;
	Entity(Scene* pScene, entt::entity id) : m_pScene(pScene), m_id(id) {}
	~Entity() = default;

	bool operator==(const Entity& other) const { return m_id == other.m_id && m_pScene == other.m_pScene; }
	bool operator!=(const Entity& other) const { return !(*this == other); }

	void Reset() { m_id = entt::null; m_pScene = nullptr; }

	[[nodiscard]] inline id_type id() const { return entt::to_integral(m_id); }
	[[nodiscard]] inline entt::entity ID() const { return m_id; }
	[[nodiscard]] inline bool IsValid() { return m_pScene->m_registry.valid(m_id) && m_pScene != nullptr; }
	[[nodiscard]] inline bool IsValid() const { return m_pScene->m_registry.valid(m_id) && m_pScene != nullptr; }

	template< typename TComponent, typename ...TArgs >
	TComponent& AttachComponent(TArgs&& ...args)
	{
		BB_ASSERT(!HasAll< TComponent >(), "Component %s is already in entity_%d!", typeid(TComponent).name(), m_id);
		return m_pScene->m_registry.emplace< TComponent >(m_id, std::forward< TArgs >(args)...);
	}

	template< typename TComponent >
	TComponent& RemoveComponent()
	{
		BB_ASSERT(HasAll< TComponent >(), "No component %s in entity_%d!", typeid(TComponent).name(), m_id);
		return m_pScene->m_registry.remove< TComponent >(m_id);
	}

	template< typename TComponent >
	TComponent& GetComponent()
	{
		BB_ASSERT(HasAll< TComponent >(), "No component %s in entity_%d!", typeid(TComponent).name(), m_id);
		return m_pScene->m_registry.get< TComponent >(m_id);
	}

	template< typename TComponent >
	const TComponent& GetComponent() const
	{
		BB_ASSERT(HasAll< TComponent >(), "No component %s in entity_%d!", typeid(TComponent).name(), m_id);
		return m_pScene->m_registry.get< TComponent >(m_id);
	}

	template< typename ...TComponents > bool HasAll() { return m_pScene->m_registry.all_of< TComponents... >(m_id); }
	template< typename ...TComponents > bool HasAll() const { return m_pScene->m_registry.all_of< TComponents... >(m_id); }
	template< typename ...TComponents > bool HasAny() { return m_pScene->m_registry.any_of< TComponents... >(m_id); }
	template< typename ...TComponents > bool HasAny() const { return m_pScene->m_registry.any_of< TComponents... >(m_id); }

private:
	Scene* m_pScene = nullptr;

	entt::entity m_id = entt::null;
};

} // namespace baamboo