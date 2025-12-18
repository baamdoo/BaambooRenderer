#pragma once
#include "../Components.h"
#include "SceneRenderView.h"

namespace baamboo
{

class EditorCamera;

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
        UNUSED(registry);
        MarkDirty(entity);
    }
    virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity)
    {
        UNUSED(registry);
        MarkDirty(entity);
    }
    virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity)
    {
        UNUSED(registry);
        m_DirtyEntities.erase(entity);
    }

    virtual std::vector< u64 > UpdateRenderData(const EditorCamera& edCamera) { UNUSED(edCamera); return {}; }
    virtual void CollectRenderData(SceneRenderView& outView) const { UNUSED(outView); }
    virtual void RemoveRenderData(u64 entityId) { UNUSED(entityId); }

protected:
    template< typename TDependency >
    void DependsOn()
    {
        RegisterDependency< TDependency >([this](entt::registry&, entt::entity entity) 
            {
				MarkDirty(entity);
            });
    }

    template< typename TDependency >
    void DependsOn(std::function< void(entt::registry&, entt::entity) > callback)
    {
        RegisterDependency<TDependency>(callback);
    }

    virtual void MarkDirty(entt::entity entity)
	{
        if (!m_Registry.valid(entity))
            return;

        m_DirtyEntities.insert(entity);
    }

    void ClearDirtyEntities()
    {
        m_DirtyEntities.clear();
    }

protected:
    entt::registry& m_Registry;

    std::unordered_set< entt::entity > m_DirtyEntities;

private:
    template< typename TDependency >
    void RegisterDependency(std::function< void(entt::registry&, entt::entity) > callback)
    {
        auto typeId = entt::type_id< TDependency >().index();
        m_DependencyCallbacks[typeId] = callback;

        m_Registry.on_construct< TDependency >().template connect< &SceneSystem::OnDependencyTriggered< TDependency > >(this);
        m_Registry.on_update< TDependency >().template connect< &SceneSystem::OnDependencyTriggered< TDependency > >(this);
    }

    template< typename TDependency >
    void OnDependencyTriggered(entt::registry& registry, entt::entity entity)
    {
        if (!registry.all_of< TComponent >(entity))
            return;

        auto typeId = entt::type_id< TDependency >().index();
        if (m_DependencyCallbacks.contains(typeId))
        {
            m_DependencyCallbacks[typeId](registry, entity);
        }
    }

    std::unordered_map< entt::id_type, std::function< void(entt::registry&, entt::entity) > > m_DependencyCallbacks;
};

} // namespace baamboo