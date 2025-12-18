#include "BaambooPch.h"
#include "LightSystem.h"
#include "TransformSystem.h"

namespace baamboo
{

//-------------------------------------------------------------------------
// Sky Light
//-------------------------------------------------------------------------
SkyLightSystem::SkyLightSystem(entt::registry& registry, TransformSystem* pTransformSystem)
	: Super(registry)
    , m_pTransformSystem(pTransformSystem)
{
    assert(m_pTransformSystem);

    DependsOn< TransformComponent >();
}

void SkyLightSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
	Super::OnComponentConstructed(registry, entity);
}

void SkyLightSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    if (registry.all_of< TransformComponent, LightComponent >(entity))
    {
        auto& component = registry.get< LightComponent >(entity);
        if (component.type == eLightType::Directional)
        {
            Super::OnComponentUpdated(registry, entity);
        }
    }
}

void SkyLightSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > SkyLightSystem::UpdateRenderData(const EditorCamera& edCamera)
{
    UNUSED(edCamera);

    std::vector< u64 > markedEntities;
    if (m_DirtyEntities.empty())
        return markedEntities;

    m_DirectionalLights.clear();
    m_Registry.view< TransformComponent, LightComponent >().each([this, &markedEntities](auto entity, auto& transformComponent, auto& lightComponent)
        {
            if (lightComponent.type == eLightType::Directional)
            {
                u64  id = entt::to_integral(entity);

                mat4   mWorld   = m_pTransformSystem->WorldMatrix(transformComponent.world);
                float3 position = float3(mWorld[3]);

                DirectionalLight dirLight = {};
                dirLight.direction      = glm::normalize(-position); // target to origin;
                dirLight.color          = lightComponent.color;
                dirLight.temperatureK   = lightComponent.temperatureK;
                dirLight.illuminanceLux = lightComponent.illuminanceLux;

                markedEntities.emplace_back(id);
                m_DirectionalLights.push_back(dirLight);
            }
        });

    ClearDirtyEntities();
    return markedEntities;
}

void SkyLightSystem::CollectRenderData(SceneRenderView& outView) const
{
    outView.light.numDirectionals = static_cast<u32>(m_DirectionalLights.size());
    for (u32 i = 0; i < outView.light.numDirectionals; ++i)
    {
        outView.light.directionals[i] = m_DirectionalLights[i];
    }
}

void SkyLightSystem::RemoveRenderData(u64 entityId)
{
    m_Registry.view< LightComponent >().each([this](auto entity, auto&)
        {
            m_DirtyEntities.insert(entity);
        });
}


//-------------------------------------------------------------------------
// Local Light
//-------------------------------------------------------------------------
LocalLightSystem::LocalLightSystem(entt::registry& registry, TransformSystem* pTransformSystem)
    : Super(registry)
    , m_pTransformSystem(pTransformSystem)
{
    assert(m_pTransformSystem);

    DependsOn< TransformComponent >();
}

void LocalLightSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentConstructed(registry, entity);
}

void LocalLightSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    if (registry.all_of< TransformComponent, LightComponent >(entity))
    {
        auto& component = registry.get< LightComponent >(entity);
        if (component.type != eLightType::Directional)
        {
            Super::OnComponentUpdated(registry, entity);
        }
    }
}

void LocalLightSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > LocalLightSystem::UpdateRenderData(const EditorCamera& edCamera)
{
    UNUSED(edCamera);

    std::vector< u64 > markedEntities;
    if (m_DirtyEntities.empty())
        return markedEntities;

    m_PointLights.clear();
    m_SpotLights.clear();

    m_Registry.view< TransformComponent, LightComponent >().each([this, &markedEntities](auto entity, auto& transformComponent, auto& lightComponent)
        {
            if (lightComponent.type != eLightType::Directional)
            {
                u64  id = entt::to_integral(entity);
                mat4 mWorld = m_pTransformSystem->WorldMatrix(transformComponent.world);

                float3 position  = float3(mWorld[3]);
                float3 direction = glm::normalize(float3(mWorld[2]));

                switch (lightComponent.type)
                {
                case eLightType::Point:
                    if (m_PointLights.size() < MAX_POINT_LIGHT)
                    {
                        PointLight light;
                        light.position       = position;
                        light.color          = lightComponent.color;
                        light.temperatureK   = lightComponent.temperatureK;
                        light.luminousFluxLm = lightComponent.luminousFluxLm;
                        light.radiusM        = lightComponent.radiusM;

                        m_PointLights.push_back(light);
                    }
                    break;

                case eLightType::Spot:
                    if (m_SpotLights.size() < MAX_SPOT_LIGHT)
                    {
                        SpotLight light;
                        light.position          = position;
                        light.direction         = direction;
                        light.color             = lightComponent.color;
                        light.temperatureK      = lightComponent.temperatureK;
                        light.luminousFluxLm    = lightComponent.luminousFluxLm;
                        light.radiusM           = lightComponent.radiusM;
                        light.innerConeAngleRad = lightComponent.innerConeAngleRad;
                        light.outerConeAngleRad = lightComponent.outerConeAngleRad;

                        m_SpotLights.push_back(light);
                    }
                    break;

                default:
                    break;
                }

                markedEntities.emplace_back(id);
            }
        });

    ClearDirtyEntities();
    return markedEntities;
}

void LocalLightSystem::CollectRenderData(SceneRenderView& outView) const
{
    outView.light.numPoints = static_cast<u32>(m_PointLights.size());
    for (u32 i = 0; i < outView.light.numPoints; ++i)
    {
        outView.light.points[i] = m_PointLights[i];
    }

    outView.light.numSpots = static_cast<u32>(m_SpotLights.size());
    for (u32 i = 0; i < outView.light.numSpots; ++i)
    {
        outView.light.spots[i] = m_SpotLights[i];
    }
}

void LocalLightSystem::RemoveRenderData(u64 entityId)
{
    m_Registry.view< LightComponent >().each([this](auto entity, auto&) 
        {
			m_DirtyEntities.insert(entity);
        });
}

}
