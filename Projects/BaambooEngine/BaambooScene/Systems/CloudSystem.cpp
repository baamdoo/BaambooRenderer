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
    auto& cloud = registry.get< CloudComponent >(entity);

    cloud.weatherMap   = TEXTURE_PATH.string() + "cloud_weather.png";
    cloud.curlNoiseTex = TEXTURE_PATH.string() + "cloud_curl_noise.png";
    cloud.blueNoiseTex = TEXTURE_PATH.string() + "blue_noise.png";

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