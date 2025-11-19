#include "BaambooPch.h"
#include "PostProcessSystem.h"

#include "AtmosphereSystem.h"

namespace baamboo
{

PostProcessSystem::PostProcessSystem(entt::registry& registry)
	: Super(registry)
{
}

void PostProcessSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
	auto& postProcess = registry.get< PostProcessComponent >(entity);

	postProcess.effectBits = 0;

	postProcess.aa.type        = eAntiAliasingType::TAA;
	postProcess.aa.blendFactor = 0.1f;
	postProcess.aa.sharpness   = 0.5f;

	postProcess.tonemap.op    = eToneMappingOp::Reinhard;
	postProcess.tonemap.gamma = 2.2f;

	Super::OnComponentConstructed(registry, entity);
}

void PostProcessSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
	Super::OnComponentUpdated(registry, entity);
}

void PostProcessSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
	Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > PostProcessSystem::Update(const EditorCamera& edCamera)
{
	UNUSED(edCamera);

	std::vector< u64 > markedEntities;
	m_Registry.view< PostProcessComponent >().each([&](auto entity, auto& postProcessComponent)
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
