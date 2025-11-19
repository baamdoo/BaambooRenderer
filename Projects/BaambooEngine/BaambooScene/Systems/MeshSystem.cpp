#include "BaambooPch.h"
#include "MeshSystem.h"

namespace baamboo
{

StaticMeshSystem::StaticMeshSystem(entt::registry& registry)
	: Super(registry)
{
}

void StaticMeshSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
	// auto& mesh = registry.get< StaticMeshComponent >(entity);
	// TODO. set default geometry and material

	Super::OnComponentConstructed(registry, entity);
}

void StaticMeshSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
	Super::OnComponentUpdated(registry, entity);
}

void StaticMeshSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
	// TODO. reduce ref-count of currently referring assets
}

std::vector< u64 > StaticMeshSystem::Update(const EditorCamera& edCamera)
{
	UNUSED(edCamera);

	std::vector< u64 > markedEntities;
	m_Registry.view< StaticMeshComponent >().each([&](auto entity, auto& meshComponent)
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