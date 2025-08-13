#include "BaambooPch.h"
#include "CloudSystem.h"

namespace baamboo
{

CloudSystem::CloudSystem(entt::registry& registry)
    : Super(registry)
{
}

void CloudSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentConstructed(registry, entity);
}

void CloudSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentUpdated(registry, entity);
}

void CloudSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > CloudSystem::Update()
{
    std::vector< u64 > markedEntities;
    m_Registry.view<CloudComponent>().each([&](auto entity, auto& cloudComponent)
        {
            if (m_DirtyEntities.contains(entity))
            {
                // The renderer will check the component's dirty flag
                m_DirtyEntities.erase(entity);
                markedEntities.push_back(entt::to_integral(entity));
            }
        });

    return markedEntities;
}

}