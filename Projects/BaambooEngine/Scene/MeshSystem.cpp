#include "BaambooPch.h"
#include "MeshSystem.h"

namespace baamboo
{

StaticMeshSystem::StaticMeshSystem(entt::registry& registry)
	: m_registry(registry)
{
	m_registry.on_construct< StaticMeshComponent >().connect< &StaticMeshSystem::OnMeshConstructed >(this);
	m_registry.on_update< StaticMeshComponent >().connect< &StaticMeshSystem::OnMeshUpdated >(this);
	m_registry.on_destroy< StaticMeshComponent >().connect< &StaticMeshSystem::OnMeshDestroyed >(this);
}

void StaticMeshSystem::OnMeshConstructed(entt::registry& registry, entt::entity entity)
{
	auto& mesh = registry.get< StaticMeshComponent >(entity);
	// TODO. set default geometry and material
	mesh.bDirtyMark = true;
}

void StaticMeshSystem::OnMeshUpdated(entt::registry& registry, entt::entity entity)
{
	auto& mesh = registry.get< StaticMeshComponent >(entity);

	mesh.bDirtyMark = true;
}

void StaticMeshSystem::OnMeshDestroyed(entt::registry& registry, entt::entity entity)
{
	// TODO. reduce ref-count of currently referring assets
}

std::vector<entt::entity> StaticMeshSystem::Update()
{
	std::vector< entt::entity > markedEntities;
	m_registry.view< StaticMeshComponent >().each([&](auto entity, auto& meshComponent)
		{
			if (meshComponent.bDirtyMark)
			{
				// TODO

				markedEntities.push_back(entity);
				meshComponent.bDirtyMark = false;
			}
		});

	return markedEntities;
}

}