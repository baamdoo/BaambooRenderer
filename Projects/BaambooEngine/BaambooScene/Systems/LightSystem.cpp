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

    const bool bHasExpired = !m_ExpiredEntities.empty();
    for (auto entity : m_ExpiredEntities)
    {
        RemoveRenderData(entt::to_integral(entity));
    }
    m_ExpiredEntities.clear();

    std::vector< u64 > markedEntities;
    if (m_DirtyEntities.empty() && !bHasExpired)
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

    const bool bHasExpired = !m_ExpiredEntities.empty();
    for (auto entity : m_ExpiredEntities)
    {
        RemoveRenderData(entt::to_integral(entity));
    }
    m_ExpiredEntities.clear();

    std::vector< u64 > markedEntities;
    if (m_DirtyEntities.empty() && !bHasExpired)
        return markedEntities;

    m_SpotLights.clear();
    m_AreaLights.clear();
    m_SphereLights.clear();
    m_DiskLights.clear();
    m_TubeLights.clear();

    m_Registry.view< TransformComponent, LightComponent >().each([this, &markedEntities](auto entity, auto& transformComponent, auto& lightComponent)
        {
            if (lightComponent.type != eLightType::Directional)
            {
                u64  id = entt::to_integral(entity);
                mat4 mWorld = m_pTransformSystem->WorldMatrix(transformComponent.world);

                float3 position = float3(mWorld[3]);

                // Light orientation is interpreted under LH-rotation convention so that the
                // user-facing rotation values follow the renderer's left-handed coord system:
                //   rotation (0, 0, 0)   -> emit forward = -Z
                //   rotation (90, 0, 0)  -> emit forward = -Y (ceiling-down)
                const float3 rotRad  = glm::radians(transformComponent.transform.rotation);
                const glm::mat4 lhRS =
                    glm::eulerAngleYXZ(-rotRad.y, -rotRad.x, -rotRad.z) *
                    glm::scale(glm::mat4(1.0f), transformComponent.transform.scale);

                const float3 colX = float3(lhRS[0]);
                const float3 colY = float3(lhRS[1]);
                const float3 colZ = float3(lhRS[2]);
                const float  sX   = glm::length(colX);
                const float  sY   = glm::length(colY);
                const float  sZ   = glm::length(colZ);

                const float3 forward = (sZ > 0.0f) ? -colZ / sZ : float3(0, 0, -1);

                switch (lightComponent.type)
                {
                case eLightType::Spot:
                    if (m_SpotLights.size() < MAX_SPOT_LIGHT)
                    {
                        SpotLight light;
                        light.position          = position;
                        light.direction         = forward; // emit forward direction
                        light.color             = lightComponent.color;
                        light.temperatureK      = lightComponent.temperatureK;
                        light.luminousFluxLm    = lightComponent.luminousFluxLm;
                        light.radiusM           = lightComponent.radiusM;
                        light.innerConeAngleRad = lightComponent.innerConeAngleRad;
                        light.outerConeAngleRad = lightComponent.outerConeAngleRad;

                        m_SpotLights.push_back(light);
                    }
                    break;

                case eLightType::Area:
                    if (m_AreaLights.size() < MAX_AREA_LIGHT)
                    {
                        AreaLight light;
                        light.position       = position;
                        light.tangent        = (sX > 0.0f) ? colX / sX : float3(1, 0, 0);
                        light.normal         = -forward; // back-face direction for shader's single-sided cull
                        light.halfWidth      = 0.5f * sX;
                        light.halfHeight     = 0.5f * sY;
                        light.color          = lightComponent.color;
                        light.luminousFluxLm = lightComponent.luminousFluxLm;
                        light.temperatureK   = lightComponent.temperatureK;

                        m_AreaLights.push_back(light);
                    }
                    break;

                case eLightType::Sphere:
                    if (m_SphereLights.size() < MAX_SPHERE_LIGHT)
                    {
                        // Sphere is isotropic; only world-scale (incl. parent) matters for radius scaling.
                        const float wsX = glm::length(float3(mWorld[0]));
                        const float wsY = glm::length(float3(mWorld[1]));
                        const float wsZ = glm::length(float3(mWorld[2]));
                        const float maxScale = std::max({ wsX, wsY, wsZ });

                        SphereLight light;
                        light.position       = position;
                        light.radius         = lightComponent.radiusM * maxScale;
                        light.color          = lightComponent.color;
                        light.luminousFluxLm = lightComponent.luminousFluxLm;
                        light.temperatureK   = lightComponent.temperatureK;

                        m_SphereLights.push_back(light);
                    }
                    break;

                case eLightType::Disk:
                    if (m_DiskLights.size() < MAX_DISK_LIGHT)
                    {
                        const float maxScale = std::max({ sX, sY, sZ });

                        DiskLight light;
                        light.position       = position;
                        light.normal         = -forward; // back-face direction (single-sided cull, Area pattern)
                        light.tangent        = (sX > 0.0f) ? colX / sX : float3(1, 0, 0);
                        light.radius         = lightComponent.diskRadiusM * maxScale;
                        light.color          = lightComponent.color;
                        light.luminousFluxLm = lightComponent.luminousFluxLm;
                        light.temperatureK   = lightComponent.temperatureK;

                        m_DiskLights.push_back(light);
                    }
                    break;

                case eLightType::Tube:
                    if (m_TubeLights.size() < MAX_TUBE_LIGHT)
                    {
                        // Tube axis follows transform's forward (Spot pattern); length scaled by sZ.
                        const float3 axis  = forward;
                        const float  halfL = 0.5f * lightComponent.tubeLengthM * sZ;

                        TubeLight light;
                        light.positionA      = position - axis * halfL;
                        light.positionB      = position + axis * halfL;
                        light.radius         = lightComponent.tubeRadiusM * std::max(sX, sY);
                        light.color          = lightComponent.color;
                        light.luminousFluxLm = lightComponent.luminousFluxLm;
                        light.temperatureK   = lightComponent.temperatureK;

                        m_TubeLights.push_back(light);
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
    outView.light.numSpots = static_cast<u32>(m_SpotLights.size());
    for (u32 i = 0; i < outView.light.numSpots; ++i)
    {
        outView.light.spots[i] = m_SpotLights[i];
    }

    outView.light.numAreas = static_cast<u32>(m_AreaLights.size());
    for (u32 i = 0; i < outView.light.numAreas; ++i)
    {
        outView.light.areas[i] = m_AreaLights[i];
    }

    outView.light.numSpheres = static_cast<u32>(m_SphereLights.size());
    for (u32 i = 0; i < outView.light.numSpheres; ++i)
    {
        outView.light.spheres[i] = m_SphereLights[i];
    }

    outView.light.numDisks = static_cast<u32>(m_DiskLights.size());
    for (u32 i = 0; i < outView.light.numDisks; ++i)
    {
        outView.light.disks[i] = m_DiskLights[i];
    }

    outView.light.numTubes = static_cast<u32>(m_TubeLights.size());
    for (u32 i = 0; i < outView.light.numTubes; ++i)
    {
        outView.light.tubes[i] = m_TubeLights[i];
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
