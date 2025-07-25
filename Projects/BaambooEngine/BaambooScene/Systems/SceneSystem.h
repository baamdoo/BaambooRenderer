#pragma once
#include "../Components.h"

namespace baamboo
{

template< typename TComponent >
class SceneSystem
{
public:
    SceneSystem(entt::registry& registry)
        : m_Registry(registry)
    {
        m_Registry.on_construct< TComponent >().connect< &SceneSystem::OnComponentConstructed >(this);
        m_Registry.on_update< TComponent >().connect< &SceneSystem::OnComponentUpdated >(this);
        m_Registry.on_destroy< TComponent >().connect< &SceneSystem::OnComponentDestroyed >(this);
    }
    virtual ~SceneSystem() = default;

    virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity)
    {
        MarkDirty(entity);
    }
    virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity)
    {
        MarkDirty(entity);
    }
    virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity)
    {
        m_DirtyEntities.erase(entity);
    }

    virtual std::vector< u64 > Update() { return std::vector< u64 >(); }

protected:
    virtual void MarkDirty(entt::entity entity)
	{
        m_DirtyEntities.insert(entity);
    }

protected:
    entt::registry& m_Registry;

    std::unordered_set< entt::entity > m_DirtyEntities;
};

} // namespace baamboo