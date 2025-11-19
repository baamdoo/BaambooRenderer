#include "BaambooPch.h"
#include "MaterialSystem.h"

namespace baamboo
{

MaterialSystem::MaterialSystem(entt::registry& registry)
	: Super(registry)
{
}

void MaterialSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
	auto& material         = registry.get< MaterialComponent >(entity);
	material.tint          = { 1, 1, 1, 1 };
	material.ambient       = { 0, 0, 0 };
	material.shininess     = 1.0f;
	material.roughness     = 1.0f;
	material.metallic      = 1.0f;
	material.ior           = 1.0f;
	material.emissivePower = 1.0f;

	Super::OnComponentConstructed(registry, entity);
}

void MaterialSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
	Super::OnComponentUpdated(registry, entity);
}

void MaterialSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
	// TODO. reduce ref-count of currently referring assets

	Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > MaterialSystem::Update(const EditorCamera& edCamera)
{
	UNUSED(edCamera);

	std::vector< u64 > markedEntities;
	m_Registry.view< MaterialComponent >().each([&](auto entity, auto& materialComponent)
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