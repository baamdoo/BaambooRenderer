#include "BaambooPch.h"
#include "AtmosphereSystem.h"
#include "LightSystem.h"

namespace baamboo
{

AtmosphereSystem::AtmosphereSystem(entt::registry& registry, SkyLightSystem* pSkyLightSystem)
	: Super(registry)
    , m_pSkyLightSystem(pSkyLightSystem)
{
    assert(m_pSkyLightSystem);

    DependsOn< TransformComponent >();
    DependsOn< LightComponent >([this](entt::registry& registry, entt::entity entity)
        {
    	    if (!registry.all_of< LightComponent, AtmosphereComponent >(entity))
    	    	return;

            auto& lightComponent = registry.get< LightComponent >(entity);
            if (lightComponent.type == eLightType::Directional)
            {
                MarkDirty(entity);
            }
        });
}

void AtmosphereSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
	auto& atmosphere = registry.get< AtmosphereComponent >(entity);

	atmosphere.planetRadiusKm     = 6360.0f;
	atmosphere.atmosphereRadiusKm = 6460.0f;

	atmosphere.rayleighScattering = { 5.802e-3f, 13.558e-3f, 33.1e-3f };
	atmosphere.rayleighDensityKm  = 8.0f;

	atmosphere.mieScattering = 3.996e-3f;
	atmosphere.mieAbsorption = 4.4e-3f;
	atmosphere.mieDensityKm  = 1.2f;
	atmosphere.miePhaseG     = 0.80f;

	atmosphere.ozoneAbsorption = { 0.650e-3f, 1.881e-3f, 0.085e-3f };
	atmosphere.ozoneCenterKm   = 25.0f;
	atmosphere.ozoneWidthKm    = 30.0f;

	atmosphere.raymarchResolution = eRaymarchResolution::Middle;

	Super::OnComponentConstructed(registry, entity);
}

void AtmosphereSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
	Super::OnComponentUpdated(registry, entity);
}

void AtmosphereSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
	Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > AtmosphereSystem::UpdateRenderData(const EditorCamera& edCamera)
{
    UNUSED(edCamera);

    std::vector< u64 > markedEntities;
    if (m_DirtyEntities.empty())
        return markedEntities;

    m_bHasData = false;
    m_Registry.view< AtmosphereComponent >().each([this, &markedEntities](auto entity, auto& component)
        {
            if (m_bHasData) 
                return;

            const auto& dirLights = m_pSkyLightSystem->GetRenderData();

            m_RenderData.id                      = entt::to_integral(entity);
            m_RenderData.data.light              = dirLights[0];
            m_RenderData.data.planetRadiusKm     = component.planetRadiusKm;
            m_RenderData.data.atmosphereRadiusKm = component.atmosphereRadiusKm;
            m_RenderData.data.rayleighScattering = component.rayleighScattering;
            m_RenderData.data.rayleighDensityKm  = component.rayleighDensityKm;
            m_RenderData.data.mieScattering      = component.mieScattering;
            m_RenderData.data.mieAbsorption      = component.mieAbsorption;
            m_RenderData.data.mieDensityKm       = component.mieDensityKm;
            m_RenderData.data.miePhaseG          = component.miePhaseG;
            m_RenderData.data.ozoneAbsorption    = component.ozoneAbsorption;
            m_RenderData.data.ozoneCenterKm      = component.ozoneCenterKm;
            m_RenderData.data.ozoneWidthKm       = component.ozoneWidthKm;
            m_RenderData.data.groundAlbedo       = float3(0.40198f);

            switch (component.raymarchResolution)
            {
            case eRaymarchResolution::Low:
                m_RenderData.msIsoSampleCount = 2;
                m_RenderData.msNumRaySteps    = 10;
                m_RenderData.svMinRaySteps    = 4;
                m_RenderData.svMaxRaySteps    = 16;
                break;
            case eRaymarchResolution::Middle:
                m_RenderData.msIsoSampleCount = 64;
                m_RenderData.msNumRaySteps    = 20;
                m_RenderData.svMinRaySteps    = 4;
                m_RenderData.svMaxRaySteps    = 32;
                break;
            case eRaymarchResolution::High:
                m_RenderData.msIsoSampleCount = 128;
                m_RenderData.msNumRaySteps    = 40;
                m_RenderData.svMinRaySteps    = 8;
                m_RenderData.svMaxRaySteps    = 64;
                break;
            default:
                __debugbreak();
                break;
            }

            m_bHasData = true;
            markedEntities.emplace_back(m_RenderData.id);
        });

    ClearDirtyEntities();
    return markedEntities;
}

void AtmosphereSystem::CollectRenderData(SceneRenderView& outView) const
{
    if (m_bHasData)
    {
        outView.atmosphere = m_RenderData;
    }
}

void AtmosphereSystem::RemoveRenderData(u64 entityId)
{
    if (m_RenderData.id == entityId)
    {
        m_bHasData = false;
    }
}

}
