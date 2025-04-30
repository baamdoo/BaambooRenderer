#include "BaambooPch.h"
#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include "TransformSystem.h"
#include "CameraSystem.h"

#include <queue>

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

Entity Scene::ImportModel(fs::path filepath, MeshDescriptor descriptor, ResourceManagerAPI& rm)
{
	auto loader = ModelLoader(filepath, descriptor);

	u32 depth = 0;
	auto rootEntity = CreateEntity(filepath.filename().string() + "_root");

	std::queue< std::pair< ModelLoader::Node*, Entity > > nodeQ;
	nodeQ.push(std::make_pair(loader.pRoot, rootEntity));
	while (!nodeQ.empty())
	{
		auto [pNode, parent] = nodeQ.front(); nodeQ.pop();

		auto nodeEntity = CreateEntity(filepath.filename().string() + "_child" + std::to_string(depth++));
		parent.AttachChild(nodeEntity.ID());

		if (!pNode->meshes.empty())
		{
			for (u32 i = 0; i < pNode->meshes.size(); ++i)
			{
				auto& meshData = pNode->meshes[i];

				// hierarchy
				auto entity = CreateEntity(meshData.name);
				nodeEntity.AttachChild(entity.ID());

				// mesh
				auto& mesh = entity.AttachComponent< StaticMeshComponent >();
				mesh.geometry.path = filepath.string();

				// geometry
				mesh.geometry.vertex =
					rm.CreateVertexBuffer(filepath.filename().wstring(), (u32)meshData.vertices.size(), sizeof(Vertex), meshData.vertices.data());
				mesh.geometry.index =
					rm.CreateIndexBuffer(filepath.filename().wstring(), (u32)meshData.indices.size(), sizeof(Index), meshData.indices.data());
				mesh.geometry.aabb = meshData.aabb;

				// material
				if (!meshData.albedoTextureFilename.empty())
				{
					mesh.material.albedo.path = filepath.relative_path().string() + meshData.albedoTextureFilename;
					mesh.material.albedo.handle = rm.CreateTexture(mesh.material.albedo.path, false);
				}
				else mesh.material.albedo.handle = eTextureIndex_DefaultWhite;

				if (!meshData.normalTextureFilename.empty())
				{
					mesh.material.normal.path = filepath.relative_path().string() + meshData.normalTextureFilename;
					mesh.material.normal.handle = rm.CreateTexture(mesh.material.normal.path, false);
				}
				else mesh.material.normal.handle = eTextureIndex_Invalid;

				if (!meshData.specularTextureFilename.empty())
				{
					mesh.material.specular.path = filepath.relative_path().string() + meshData.specularTextureFilename;
					mesh.material.specular.handle = rm.CreateTexture(mesh.material.specular.path, false);
				}
				else mesh.material.specular.handle = eTextureIndex_DefaultWhite;

				if (!meshData.emissiveTextureFilename.empty())
				{
					mesh.material.emission.path = filepath.relative_path().string() + meshData.emissiveTextureFilename;
					mesh.material.emission.handle = rm.CreateTexture(mesh.material.emission.path, false);
				}
				else mesh.material.emission.handle = eTextureIndex_DefaultBlack;

				if (!meshData.aoTextureFilename.empty())
				{
					mesh.material.ao.path = filepath.relative_path().string() + meshData.aoTextureFilename;
					mesh.material.ao.handle = rm.CreateTexture(mesh.material.ao.path, false);
				}
				else mesh.material.ao.handle = eTextureIndex_DefaultBlack;

				if (!meshData.roughnessTextureFilename.empty())
				{
					mesh.material.roughness.path = filepath.relative_path().string() + meshData.roughnessTextureFilename;
					mesh.material.roughness.handle = rm.CreateTexture(mesh.material.roughness.path, false);
				}
				else mesh.material.roughness.handle = eTextureIndex_DefaultWhite;

				if (!meshData.metallicTextureFilename.empty())
				{
					mesh.material.metallic.path = filepath.relative_path().string() + meshData.metallicTextureFilename;
					mesh.material.metallic.handle = rm.CreateTexture(mesh.material.metallic.path, false);
				}
				else mesh.material.metallic.handle = eTextureIndex_DefaultWhite;
			}
		}

		for (auto& pChild : pNode->pChilds)
			nodeQ.push(std::make_pair(pChild, nodeEntity));
	}

	return rootEntity;
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
			meshView.geometry.vb = meshComponent.geometry.vertex.vb;
			meshView.geometry.vOffset = meshComponent.geometry.vertex.vOffset;
			meshView.geometry.vCount = meshComponent.geometry.vertex.vCount;
			meshView.geometry.ib = meshComponent.geometry.index.ib;
			meshView.geometry.iOffset = meshComponent.geometry.index.iOffset;
			meshView.geometry.iCount = meshComponent.geometry.index.iCount;

			meshView.material.tint = meshComponent.material.tint;
			meshView.material.albedo = meshComponent.material.albedo.handle;
			meshView.material.normal = meshComponent.material.normal.handle;
			meshView.material.specular = meshComponent.material.specular.handle;
			meshView.material.ao = meshComponent.material.ao.handle;
			meshView.material.roughness = meshComponent.material.roughness.handle;
			meshView.material.metallic = meshComponent.material.metallic.handle;
			meshView.material.emission = meshComponent.material.emission.handle;
			view.meshes.push_back(meshView);

			assert(view.draws.count(meshView.id)); // all entity has transform and must be parsed first!
			auto& draw = view.draws.find(meshView.id)->second;
			draw.mesh = static_cast<u32>(view.meshes.size()) - 1;
		});

	return view;
}

} // namespace baamboo
