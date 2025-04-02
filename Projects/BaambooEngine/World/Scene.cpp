#include "BaambooPch.h"
#include "Scene.h"
#include "Entity.h"
#include "Components.h"

namespace baamboo
{

Scene::Scene(const std::string& name)
	: m_name(name)
{
}

Entity Scene::CreateEntity(const std::string& tag)
{
	Entity newEntity = Entity(this, m_registry.create());
	newEntity.AttachComponent< TagComponent >(tag);

	printf("create entity_%d\n", newEntity.id());

	SortEntities();

	return newEntity;
}

void Scene::RemoveEntity(Entity entity)
{
	printf("remove entity_%d\n", entity.id());
	m_registry.destroy(entity.ID());
}

void Scene::SortEntities()
{
	m_registry.sort< TransformComponent >([](const auto lhs, const auto rhs)
		{
			return lhs.transform.Depth() < rhs.transform.Depth();
		});
}

}
