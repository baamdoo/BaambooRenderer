#include "BaambooPch.h"
#include "MaterialSystem.h"

namespace baamboo
{

MaterialSystem::MaterialSystem(entt::registry& registry)
	: m_registry(registry)
{
	m_registry.on_construct< MaterialComponent >().connect< &MaterialSystem::OnMaterialConstructed >(this);
	m_registry.on_update< MaterialComponent >().connect< &MaterialSystem::OnMaterialUpdated >(this);
	m_registry.on_destroy< MaterialComponent >().connect< &MaterialSystem::OnMaterialDestroyed >(this);
}

void MaterialSystem::OnMaterialConstructed(entt::registry& registry, entt::entity entity)
{
	auto& material = registry.get< MaterialComponent >(entity);
	material.tint = { 1, 1, 1 };
	material.roughness = 1.0f;
	material.metallic = 0.0f;
	material.bDirtyMark = true;
}

void MaterialSystem::OnMaterialUpdated(entt::registry& registry, entt::entity entity)
{
	auto& mesh = registry.get< MaterialComponent >(entity);

	mesh.bDirtyMark = true;
}

void MaterialSystem::OnMaterialDestroyed(entt::registry& registry, entt::entity entity)
{
	// TODO. reduce ref-count of currently referring assets
}

std::vector<entt::entity> MaterialSystem::Update()
{
	std::vector< entt::entity > markedEntities;
	m_registry.view< MaterialComponent >().each([&](auto entity, auto& materialComponent)
		{
			if (materialComponent.bDirtyMark)
			{
				// TODO

				markedEntities.push_back(entity);
				materialComponent.bDirtyMark = false;
			}
		});

	return markedEntities;
}

}