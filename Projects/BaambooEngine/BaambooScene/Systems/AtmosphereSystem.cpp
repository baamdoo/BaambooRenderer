#include "BaambooPch.h"
#include "AtmosphereSystem.h"

namespace baamboo
{

AtmosphereSystem::AtmosphereSystem(entt::registry& registry)
	: Super(registry)
{
}

void AtmosphereSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
	auto& atmosphere = registry.get< AtmosphereComponent >(entity);

	atmosphere.planetRadiusKm     = 6360.0f;
	atmosphere.atmosphereRadiusKm = 6460.0f;

	atmosphere.rayleighScattering  = { 5.802e-3f, 13.558e-3f, 33.1e-3f };
	atmosphere.rayleighDensityKm = 8.0f;

	atmosphere.mieScattering  = 3.996e-3f;
	atmosphere.mieAbsorption  = 4.4e-3f;
	atmosphere.mieDensityKm = 1.2f;
	atmosphere.miePhaseG      = 0.80f;

	atmosphere.ozoneAbsorption = { 0.650e-3f, 1.881e-3f, 0.085e-3f };
	atmosphere.ozoneCenterKm  = 25.0f;
	atmosphere.ozoneWidthKm   = 30.0f;

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

std::vector< u64 > AtmosphereSystem::Update(const EditorCamera& edCamera)
{
	UNUSED(edCamera);

	std::vector< u64 > markedEntities;
	m_Registry.view< AtmosphereComponent >().each([&](auto entity, auto& atmosphereComponent)
		{
			if (m_DirtyEntities.contains(entity))
			{
				// TODO

				m_DirtyEntities.erase(entity);
				markedEntities.push_back(entt::to_integral(entity));
			}
		});

	return markedEntities;
}

}