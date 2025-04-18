#include "BaambooPch.h"
#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include "TransformSystem.h"
#include "CameraSystem.h"

namespace baamboo
{

Scene::Scene(const std::string& name)
	: m_name(name)
{
	m_pTransformSystem = new TransformSystem(m_registry);
	m_pCameraSystem = new CameraSystem(m_registry);
}

Scene::~Scene()
{
	RELEASE(m_pCameraSystem);
	RELEASE(m_pTransformSystem);
}

Entity Scene::CreateEntity(const std::string& tag)
{
	Entity newEntity = Entity(this, m_registry.create());
	newEntity.AttachComponent< TagComponent >(tag);
	newEntity.AttachComponent< TransformComponent >();

	printf("create entity_%d\n", newEntity.id());

	return newEntity;
}

void Scene::RemoveEntity(Entity entity)
{
	printf("remove entity_%d\n", entity.id());

	if (entity.GetComponent< TransformComponent >().hierarchy.parent != entt::null)
		m_pTransformSystem->DetachChild(entity.ID());

	entt::entity child = entity.GetComponent< TransformComponent >().hierarchy.firstChild;
	while (child != entt::null)
	{
		auto childEntity = Entity(this, child);
		auto& transformComponent = childEntity.GetComponent< TransformComponent >();
		transformComponent.hierarchy.parent = entt::null;

		RemoveEntity(childEntity);
		child = transformComponent.hierarchy.nextSibling;
	}

	m_registry.destroy(entity.ID());
}

void Scene::Update(f32 dt)
{
	m_pTransformSystem->Update();
}

SceneRenderView Scene::RenderView() const
{
	SceneRenderView view{};
	m_registry.view< TransformComponent >().each([this, &view](auto id, auto& transformComponent)
		{
			TransformRenderView transformView = {};
			transformView.id = entt::to_integral(id);
			transformView.mWorld = m_pTransformSystem->WorldMatrix(transformComponent.mWorld);
			view.transforms.push_back(transformView);

			DrawData draw = {};
			draw.transform = static_cast<u32>(view.transforms.size()) - 1;
			view.draws.emplace(transformView.id, draw);
		});

	m_registry.view< TransformComponent, CameraComponent >().each([this, &view](auto id, auto& transformComponent, auto& cameraComponent)
		{
			CameraRenderView cameraView = {};
			cameraView.id = entt::to_integral(id);
			cameraView.mProj = m_pCameraSystem->ProjMatrix();
			cameraView.mView = m_pCameraSystem->ViewMatrix();
			cameraView.pos = transformComponent.transform.position;
			view.cameras.push_back(cameraView);

			assert(view.draws.count(cameraView.id)); // all entity has transform and must be parsed first!
			auto& draw = view.draws.find(cameraView.id)->second;
			draw.camera = static_cast<u32>(view.cameras.size()) - 1;
		});

	m_registry.view< StaticMeshComponent >().each([this, &view](auto id, auto& meshComponent)
		{
			StaticMeshRenderView meshView = {};
			meshView.id = entt::to_integral(id);
			meshView.geometry = meshComponent.geometry;
			view.meshes.push_back(meshView);

			assert(view.draws.count(meshView.id)); // all entity has transform and must be parsed first!
			auto& draw = view.draws.find(meshView.id)->second;
			draw.mesh = static_cast<u32>(view.meshes.size()) - 1;
		});

	return view;
}

} // namespace baamboo
