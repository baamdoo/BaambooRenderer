#include "BaambooPch.h"
#include "Entity.h"

namespace baamboo
{

Entity Entity::Clone()
{
	assert(IsValid());
	auto clonedEntity = m_pScene->CreateEntity(GetComponent< TagComponent >().tag + "_Clone");

	auto fpCloneEntity = [this](Entity original, Entity cloned)
		{
			auto& registry = m_pScene->m_Registry;

			if (original.HasAll< TagComponent >())
			{
				auto& orgComponent = original.GetComponent< TagComponent >();
				auto& newComponent = cloned.GetComponent< TagComponent >();
				newComponent = orgComponent;

				if (original == *this)
					newComponent.tag = orgComponent.tag + "_Clone";
			}

			if (original.HasAll< TransformComponent >())
			{
				auto& orgComponent = original.GetComponent< TransformComponent >();
				auto& newComponent = cloned.GetComponent< TransformComponent >();
				newComponent.transform = orgComponent.transform;

				// cloned entity always be root
				if (original == *this)
				{
					newComponent.hierarchy.parent = entt::null;
				}
			}

			if (original.HasAll< CameraComponent >())
			{
				cloned.AttachComponent< CameraComponent >();

				auto& orgComponent = original.GetComponent< CameraComponent >();
				auto& newComponent = cloned.GetComponent< CameraComponent >();
				newComponent = orgComponent;
			}

			if (original.HasAll< StaticMeshComponent >())
			{
				cloned.AttachComponent< StaticMeshComponent >();

				auto& orgComponent = original.GetComponent< StaticMeshComponent >();
				auto& newComponent = cloned.GetComponent< StaticMeshComponent >();
				newComponent = orgComponent;
			}

			if (original.HasAll< DynamicMeshComponent >())
			{
				auto& orgComponent = original.GetComponent< DynamicMeshComponent >();
				cloned.AttachComponent< DynamicMeshComponent >();

				auto& newComponent = cloned.GetComponent< DynamicMeshComponent >();
				newComponent = orgComponent;
			}

			if (original.HasAll< MaterialComponent >())
			{
				cloned.AttachComponent< MaterialComponent >();

				auto& orgComponent = original.GetComponent< MaterialComponent >();
				auto& newComponent = cloned.GetComponent< MaterialComponent >();
				newComponent = orgComponent;
			}

			if (original.HasAll< LightComponent >())
			{
				cloned.AttachComponent< LightComponent >();

				auto& orgComponent = original.GetComponent< LightComponent >();
				auto& newComponent = cloned.GetComponent< LightComponent >();
				newComponent = orgComponent;
			}
		};
	fpCloneEntity(*this, clonedEntity);

	auto& orgTransform = GetComponent< TransformComponent >();
	if (orgTransform.hierarchy.firstChild != entt::null)
	{

		std::function< void(Entity, Entity) > fpCloneChildEntity = [&](Entity originalParent, Entity clonedParent)
			{
				auto& registry = m_pScene->m_Registry;

				// search all children of the original parent
				auto view = registry.view< TransformComponent >();
				for (auto entity : view)
				{
					auto& transform = view.get< TransformComponent >(entity);
					if (transform.hierarchy.parent == originalParent.ID())
					{
						// Create child entity
						Entity orgChild = Entity(m_pScene, entity);
						Entity clnChild = m_pScene->CreateEntity(orgChild.GetComponent< TagComponent >().tag);

						fpCloneEntity(orgChild, clnChild);

						clonedParent.AttachChild(clnChild.ID());

						fpCloneChildEntity(orgChild, clnChild);
					}
				}
			};

		// Clone all children
		fpCloneChildEntity(*this, clonedEntity);
	}

	return clonedEntity;
}

} // namespace baamboo