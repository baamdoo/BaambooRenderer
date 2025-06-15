#include "BaambooPch.h"
#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include "Camera.h"
#include "TransformSystem.h"
#include "MeshSystem.h"
#include "MaterialSystem.h"

#include <queue>

namespace baamboo
{

Scene::Scene(const std::string& name)
	: m_Name(name)
{
	m_pTransformSystem  = new TransformSystem(m_Registry);
	m_pStaticMeshSystem = new StaticMeshSystem(m_Registry);
	m_pMaterialSystem   = new MaterialSystem(m_Registry);
}

Scene::~Scene()
{
	RELEASE(m_pMaterialSystem);
	RELEASE(m_pStaticMeshSystem);
	RELEASE(m_pTransformSystem);
}

Entity Scene::CreateEntity(const std::string& tag)
{
	Entity newEntity = Entity(this, m_Registry.create());
	newEntity.AttachComponent< TagComponent >(tag);
	newEntity.AttachComponent< TransformComponent >();

	printf("create entity_%d\n", newEntity.id());

	m_EntityDirtyMasks.emplace(std::make_pair(newEntity.ID(), 0));
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

	m_Registry.destroy(entity.ID());
	m_EntityDirtyMasks.erase(entity.ID());
}

Entity Scene::ImportModel(fs::path filepath, MeshDescriptor descriptor)
{
	auto rootEntity = CreateEntity(filepath.filename().string() + "_root");

	return ImportModel(rootEntity, filepath, descriptor);
}

Entity Scene::ImportModel(Entity rootEntity, fs::path filepath, MeshDescriptor descriptor)
{
	u32 depth = 0;
	auto loader = ModelLoader(filepath, descriptor);

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
				mesh.path = filepath.string();

				// geometry
				mesh.vertices = std::move(meshData.vertices);
				mesh.indices = std::move(meshData.indices);

				// material
				auto& material = entity.AttachComponent< MaterialComponent >();
				if (!meshData.albedoTextureFilename.empty())
				{
					material.albedoTex = filepath.parent_path().string() + "/" + meshData.albedoTextureFilename;
				}

				if (!meshData.normalTextureFilename.empty())
				{
					material.normalTex = filepath.parent_path().string() + "/" + meshData.normalTextureFilename;
				}

				if (!meshData.specularTextureFilename.empty())
				{
					material.specularTex = filepath.parent_path().string() + "/" + meshData.specularTextureFilename;
				}

				if (!meshData.emissiveTextureFilename.empty())
				{
					material.emissionTex = filepath.parent_path().string() + "/" + meshData.emissiveTextureFilename;
				}

				if (!meshData.aoTextureFilename.empty())
				{
					material.aoTex = filepath.parent_path().string() + "/" + meshData.aoTextureFilename;
				}

				if (!meshData.roughnessTextureFilename.empty())
				{
					material.roughnessTex = filepath.parent_path().string() + "/" + meshData.roughnessTextureFilename;
				}

				if (!meshData.metallicTextureFilename.empty())
				{
					material.metallicTex = filepath.parent_path().string() + "/" + meshData.metallicTextureFilename;
				}
			}
		}

		for (auto& pChild : pNode->pChilds)
			nodeQ.push(std::make_pair(pChild, nodeEntity));
	}

	return rootEntity;
}

void Scene::Update(f32 dt)
{
	auto markedEntities = m_pTransformSystem->Update();
	for (auto entity : markedEntities)
	{
		u64& dirtyMarks = m_EntityDirtyMasks[entity];
		dirtyMarks |= (1 << eComponentType::CTransform);
	}

	markedEntities = m_pStaticMeshSystem->Update();
	for (auto entity : markedEntities)
	{
		u64& dirtyMarks = m_EntityDirtyMasks[entity];
		dirtyMarks |= (1 << eComponentType::CStaticMesh);
	}

	markedEntities = m_pMaterialSystem->Update();
	for (auto entity : markedEntities)
	{
		u64& dirtyMarks = m_EntityDirtyMasks[entity];
		dirtyMarks |= (1 << eComponentType::CMaterial);
	}
}

SceneRenderView Scene::RenderView(const EditorCamera& camera) const
{
	SceneRenderView view{};
	view.camera.mView = camera.GetView();
	view.camera.mProj = camera.GetProj();
	view.camera.pos   = camera.GetPosition();

	m_Registry.view< TransformComponent >().each([this, &view](auto id, auto& transformComponent)
		{
			TransformRenderView transformView = {};
			transformView.id     = entt::to_integral(id);
			transformView.mWorld = m_pTransformSystem->WorldMatrix(transformComponent.world);
			view.transforms.push_back(transformView);

			DrawRenderView draw = {};
			draw.transform = static_cast<u32>(view.transforms.size()) - 1;
			view.draws.emplace(transformView.id, draw);
		});

	m_Registry.view< TagComponent, StaticMeshComponent, MaterialComponent >().each([this, &view](auto id, auto& tagComponent, auto& meshComponent, auto& materialComponent)
		{
			StaticMeshRenderView meshView = {};
			meshView.id     = entt::to_integral(id);
			meshView.tag    = tagComponent.tag;
			meshView.vData  = meshComponent.vertices.data();
			meshView.vCount = static_cast<u32>(meshComponent.vertices.size());
			meshView.iData  = meshComponent.indices.data();
			meshView.iCount = static_cast<u32>(meshComponent.indices.size());
			view.meshes.push_back(meshView);

			/*if (!view.draws.contains(meshView.id))
			{
				view.draws.emplace(meshView.id, DrawRenderView{});
			}*/
			assert(view.draws.contains(meshView.id));
			auto& draw = view.draws.find(meshView.id)->second;
			draw.mesh = static_cast<u32>(view.meshes.size()) - 1;

			MaterialRenderView materialView = {};
			materialView.id        = entt::to_integral(id);
			materialView.tint      = materialComponent.tint;
			materialView.roughness = materialComponent.roughness;
			materialView.metallic  = materialComponent.metallic;

			materialView.albedoTex    = materialComponent.albedoTex;
			materialView.normalTex    = materialComponent.normalTex;
			materialView.specularTex  = materialComponent.specularTex;
			materialView.aoTex        = materialComponent.aoTex;
			materialView.roughnessTex = materialComponent.roughnessTex;
			materialView.metallicTex  = materialComponent.metallicTex;
			materialView.emissionTex  = materialComponent.emissionTex;
			view.materials.push_back(materialView);

			/*if (!view.draws.contains(materialView.id))
			{
				view.draws.emplace(materialView.id, DrawRenderView{});
			}*/
			draw.material = static_cast<u32>(view.materials.size()) - 1;
		});


	view.light.data = {};
	view.light.data.ambientColor = float3(0.0f);
	view.light.data.ambientIntensity = 0.0f;
	m_Registry.view< TransformComponent, LightComponent >().each([this, &view](auto id, auto& transformComponent, auto& lightComponent)
		{
			mat4   mWorld    = m_pTransformSystem->WorldMatrix(transformComponent.world);
			float3 position  = float3(mWorld[3]);
			float3 direction = normalize(float3(mWorld[2]));

			switch (lightComponent.type)
			{
			case eLightType::Directional:
				if (view.light.data.numDirectionals < MAX_DIRECTIONAL_LIGHT)
				{
					auto& dirLight             = view.light.data.directionals[view.light.data.numDirectionals++];
					dirLight.direction         = -position; // target to origin
					dirLight.color             = lightComponent.color;
					dirLight.temperature_K     = lightComponent.temperature_K;
					dirLight.illuminance_lux   = lightComponent.illuminance_lux;
					dirLight.angularRadius_rad = lightComponent.angularRadius_rad;
				}
				break;

			case eLightType::Point:
				if (view.light.data.numPoints < MAX_POINT_LIGHT)
				{
					PointLight& pointLight      = view.light.data.points[view.light.data.numPoints++];
					pointLight.position         = position;
					pointLight.color            = lightComponent.color;
					pointLight.temperature_K    = lightComponent.temperature_K;
					pointLight.luminousPower_lm = lightComponent.luminousPower_lm;
					pointLight.radius_m         = lightComponent.radius_m;
				}
				break;

			case eLightType::Spot:
				if (view.light.data.numSpots < MAX_SPOT_LIGHT)
				{
					auto& spotLight              = view.light.data.spots[view.light.data.numSpots++];
					spotLight.position           = position;
					spotLight.direction          = direction;
					spotLight.color              = lightComponent.color;
					spotLight.temperature_K      = lightComponent.temperature_K;
					spotLight.luminousPower_lm   = lightComponent.luminousPower_lm;
					spotLight.radius_m           = lightComponent.radius_m;
					spotLight.innerConeAngle_rad = lightComponent.innerConeAngle_rad;
					spotLight.outerConeAngle_rad = lightComponent.outerConeAngle_rad;
				}
				break;
			}
		});

	return view;
}

} // namespace baamboo
