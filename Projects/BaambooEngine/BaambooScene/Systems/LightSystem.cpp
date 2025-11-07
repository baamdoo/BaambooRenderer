#include "BaambooPch.h"
#include "LightSystem.h"

namespace baamboo
{

SkyLightSystem::SkyLightSystem(entt::registry& registry)
    : Super(registry)
{
}

void SkyLightSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get< LightComponent >(entity);

    Super::OnComponentConstructed(registry, entity);
}

void SkyLightSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentUpdated(registry, entity);
}

void SkyLightSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > SkyLightSystem::Update()
{
    std::vector< u64 > markedEntities;
    m_Registry.view< LightComponent >().each([&](auto entity, auto& component)
        {
            if (m_DirtyEntities.contains(entity))
            {
                // The renderer will check the component's dirty flag
                m_DirtyEntities.erase(entity);
                if (component.type == eLightType::Directional)
                {
                    markedEntities.push_back(entt::to_integral(entity));
                }
            }
        });

    return markedEntities;
}


LocalLightSystem::LocalLightSystem(entt::registry& registry)
    : Super(registry)
{
}

void LocalLightSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get< LightComponent >(entity);

    Super::OnComponentConstructed(registry, entity);
}

void LocalLightSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentUpdated(registry, entity);
}

void LocalLightSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > LocalLightSystem::Update()
{
    std::vector< u64 > markedEntities;
    m_Registry.view< LightComponent >().each([&](auto entity, auto& component)
        {
            if (m_DirtyEntities.contains(entity))
            {
                // The renderer will check the component's dirty flag
                m_DirtyEntities.erase(entity);
                if (component.type != eLightType::Directional)
                {
                    markedEntities.push_back(entt::to_integral(entity));
                }
            }
        });

    return markedEntities;
}

}