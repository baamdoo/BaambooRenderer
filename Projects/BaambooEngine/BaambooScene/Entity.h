#pragma once
#include "Scene.h"
#include "Systems/TransformSystem.h"

namespace baamboo
{

class Entity
{
public:
	using id_type = ENTT_ID_TYPE;

	Entity() = default;
	Entity(Scene* pScene, entt::entity id) : m_pScene(pScene), m_id(id) {}

	bool operator==(const Entity& other) const { return m_id == other.m_id && m_pScene == other.m_pScene; }
	bool operator!=(const Entity& other) const { return !(*this == other); }

	operator bool() const { return m_id != entt::null; }
	operator entt::entity() const { return m_id; }
	operator u32() const { return (u32)m_id; }

	void Reset() { m_id = entt::null; m_pScene = nullptr; }

	[[nodiscard]] inline id_type id() const { return entt::to_integral(m_id); }
	[[nodiscard]] inline entt::entity ID() const { return m_id; }
	[[nodiscard]] inline bool IsValid() { return m_pScene != nullptr && m_pScene->m_Registry.valid(m_id); }
	[[nodiscard]] inline bool IsValid() const { return m_pScene != nullptr && m_pScene->m_Registry.valid(m_id); }

	template< typename TComponent, typename ...TArgs >
	TComponent& AttachComponent(TArgs&& ...args)
	{
		BB_ASSERT(!HasAll< TComponent >(), "Component %s is already in entity_%d!", typeid(TComponent).name(), m_id);
		return m_pScene->m_Registry.emplace< TComponent >(m_id, std::forward< TArgs >(args)...);
	}

	template< typename TComponent >
	size_t RemoveComponent()
	{
		BB_ASSERT(HasAll< TComponent >(), "No component %s in entity_%d!", typeid(TComponent).name(), m_id);
		return m_pScene->m_Registry.remove< TComponent >(m_id);
	}

	template< typename TComponent >
	TComponent& GetComponent()
	{
		BB_ASSERT(HasAll< TComponent >(), "No component %s in entity_%d!", typeid(TComponent).name(), m_id);
		return m_pScene->m_Registry.get< TComponent >(m_id);
	}

	template< typename TComponent >
	const TComponent& GetComponent() const
	{
		BB_ASSERT(HasAll< TComponent >(), "No component %s in entity_%d!", typeid(TComponent).name(), m_id);
		return m_pScene->m_Registry.get< TComponent >(m_id);
	}

	template< typename ...TComponents > bool HasAll() { return m_pScene->m_Registry.all_of< TComponents... >(m_id); }
	template< typename ...TComponents > bool HasAll() const { return m_pScene->m_Registry.all_of< TComponents... >(m_id); }
	template< typename ...TComponents > bool HasAny() { return m_pScene->m_Registry.any_of< TComponents... >(m_id); }
	template< typename ...TComponents > bool HasAny() const { return m_pScene->m_Registry.any_of< TComponents... >(m_id); }

	void AttachChild(entt::entity id)
	{
		BB_ASSERT(HasAll< TransformComponent >() && m_pScene->m_Registry.all_of< TransformComponent >(id), "Only entity with TransformComponent can have hierarchical traits!");
		assert(m_pScene->GetTransformSystem());

		m_pScene->GetTransformSystem()->AttachChild(m_id, id);
	}
	void DetachChild()
	{
		BB_ASSERT(HasAll< TransformComponent >(), "Only entity with TransformComponent can have hierarchical traits!");
		assert(m_pScene->GetTransformSystem());

		m_pScene->GetTransformSystem()->DetachChild(m_id);
	}

	[[nodiscard]]
	Entity Clone();

private:
	Scene* m_pScene = nullptr;

	entt::entity m_id = entt::null;
};

} // namespace baamboo