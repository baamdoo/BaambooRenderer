#include "BaambooPch.h"
#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include "Camera.h"
#include "TransformSystem.h"
#include "MeshSystem.h"
#include "MaterialSystem.h"

#include <queue>
#include <glm/gtx/matrix_decompose.hpp>

namespace baamboo
{

static std::unordered_map< std::string, Entity > s_ModelCache;

Scene::Scene(const std::string& name)
	: m_Name(name)
{
	m_pTransformSystem  = new TransformSystem(m_Registry);
	m_pStaticMeshSystem = new StaticMeshSystem(m_Registry);
	m_pMaterialSystem   = new MaterialSystem(m_Registry);
}

Scene::~Scene()
{
	for (auto& [_, pLoader] : m_ModelLoaderCache)
		RELEASE(pLoader);

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

Entity Scene::ImportModel(const fs::path& filepath, MeshDescriptor descriptor)
{
	return ImportModel(CreateEntity(filepath.filename().string() + "_Root"), filepath, descriptor);
}

Entity Scene::ImportModel(Entity rootEntity, const fs::path& filepath, MeshDescriptor descriptor)
{
	if (auto it = s_ModelCache.find(filepath.string()); it != s_ModelCache.end())
	{
		return it->second.Clone();
	}

	m_bLoading = true;

	auto pLoader   = new ModelLoader(filepath, descriptor);
	auto pRootNode = pLoader->GetRootNode();
	m_ModelLoaderCache.emplace(filepath.string(), pLoader);

	const AnimationData& animData = pLoader->GetAnimationData();
	u32 skeletonID = INVALID_INDEX;

	// If model has animations, create skeleton and animation components
	if (pLoader->HasAnimations())
	{
		// Store skeleton (you'll need to add skeleton storage to Scene or a dedicated AnimationSystem)
		skeletonID = StoreSkeletonData(animData.skeleton);

		// Add skeleton component to root entity
		rootEntity.AttachComponent< SkeletonComponent >().skeletonID = skeletonID;

		// Add animation component if there are clips
		if (!animData.clips.empty())
		{
			auto& animComp = rootEntity.AttachComponent<AnimationComponent>();
			animComp.skeletonID = skeletonID;
			animComp.currentClipID = 0; // Default to first clip
			animComp.currentPose.boneTransforms.resize(animData.skeleton.bones.size());
			animComp.currentPose.mBones.resize(animData.skeleton.bones.size());

			// Initialize pose to bind pose
			for (u64 i = 0; i < animData.skeleton.bones.size(); ++i)
			{
				animComp.currentPose.boneTransforms[i] = BoneTransform();
				animComp.currentPose.mBones[i]         = mat4(1.0f);
			}
		}

		// Store animation clips
		for (const auto& clip : animData.clips)
		{
			StoreAnimationClip(clip);
		}
	}

	std::string parentPath = filepath.parent_path().string() + "/";
	std::function< void(const ModelNode*, Entity) > ProcessNode = [&](const ModelNode* node, Entity parent)
		{
			Entity entity = CreateEntity(node->name);

			// Hierarchy
			if (parent.IsValid())
			{
				parent.AttachChild(entity.ID());
			}

			// Transform
			TransformComponent& transformComponent = entity.GetComponent< TransformComponent >();

			float3 scale, translation, skew;
			float4 perspective;
			quat rotation;
			glm::decompose(node->mTransform, scale, rotation, translation, skew, perspective);

			transformComponent.transform.position = translation;
			transformComponent.transform.rotation = glm::eulerAngles(rotation);
			transformComponent.transform.scale    = scale;

			// Process meshes
			for (u32 meshIndex : node->meshIndices)
			{
				const MeshData& meshData = pLoader->GetMeshes()[meshIndex];

				Entity meshEntity = CreateEntity(meshData.name);
				entity.AttachChild(meshEntity.ID());

				if (meshData.bHasSkinnedData && skeletonID != INVALID_INDEX)
				{
					// skinned mesh
					auto& mesh      = meshEntity.AttachComponent< SkinnedMeshComponent >();
					mesh.skeletonID = skeletonID;
					mesh.meshID     = StoreMeshData(meshData);
				}
				else
				{
					// static mesh
					auto& mesh = meshEntity.AttachComponent< StaticMeshComponent >();
					mesh.path  = filepath.string();

					mesh.numVertices  = static_cast<u32>(meshData.vertices.size());
					mesh.numIndices   = static_cast<u32>(meshData.indices.size());
					mesh.pVertices    = const_cast<Vertex*>(meshData.vertices.data());
					if (mesh.numIndices > 0)
						mesh.pIndices = const_cast<Index*>(meshData.indices.data());
				}

				// Material
				auto& material = meshEntity.AttachComponent< MaterialComponent >();
				if (meshData.materialIndex < pLoader->GetMaterials().size())
				{
					const MaterialData& matData = pLoader->GetMaterials()[meshData.materialIndex];

					material.name = matData.name;

					material.tint      = float4(matData.diffuse, 1.0f);
					material.metallic  = matData.metallic;
					material.roughness = matData.roughness;

					material.albedoTex    = (matData.albedoPath.empty() ? "" : parentPath) + matData.albedoPath;
					material.normalTex    = (matData.normalPath.empty() ? "" : parentPath) + matData.normalPath;
					material.metallicTex  = (matData.metallicPath.empty() ? "" : parentPath) + matData.metallicPath;
					material.roughnessTex = (matData.roughnessPath.empty() ? "" : parentPath) + matData.roughnessPath;
					material.aoTex        = (matData.aoPath.empty() ? "" : parentPath) + matData.aoPath;
					material.emissionTex  = (matData.emissivePath.empty() ? "" : parentPath) + matData.emissivePath;
				}
			}

			// Process children
			for (const auto& pChild : node->pChilds)
			{
				ProcessNode(pChild, entity);
			}
		};

	ProcessNode(pRootNode, rootEntity);

	m_bLoading = false;

	s_ModelCache.emplace(filepath.string(), rootEntity);
	return rootEntity;
}

u32 Scene::StoreMeshData(const MeshData& meshData)
{
	static u32 nextMeshID = 0;
	u32 id = nextMeshID++;
	m_MeshData[id] = meshData;
	return id;
}

u32 Scene::StoreSkeletonData(const Skeleton& skeleton)
{
	static u32 nextSkeletonID = 0;
	u32 id = nextSkeletonID++;
	m_Skeletons[id] = skeleton;
	return id;
}

u32 Scene::StoreAnimationClip(const AnimationClip& clip)
{
	static u32 nextClipID = 0;
	u32 id = nextClipID++;
	m_AnimationClips[id] = clip;
	return id;
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
			meshView.vData  = meshComponent.pVertices;
			meshView.vCount = meshComponent.numVertices;
			meshView.iData  = meshComponent.pIndices;
			meshView.iCount = meshComponent.numIndices;
			view.meshes.push_back(meshView);

			/*if (!view.draws.contains(meshView.id))
			{
				view.draws.emplace(meshView.id, DrawRenderView{});
			}*/
			assert(view.draws.contains(meshView.id));
			auto& draw = view.draws.find(meshView.id)->second;
			draw.mesh  = static_cast<u32>(view.meshes.size()) - 1;

			MaterialRenderView materialView = {};
			materialView.id = entt::to_integral(id);

			materialView.tint     = materialComponent.tint;
			materialView.ambient  = materialComponent.ambient;

			materialView.shininess     = materialComponent.shininess;
			materialView.roughness     = materialComponent.roughness;
			materialView.metallic      = materialComponent.metallic;
			materialView.ior           = materialComponent.ior;
			materialView.emissivePower = materialComponent.emissivePower;

			materialView.albedoTex    = materialComponent.albedoTex;
			materialView.normalTex    = materialComponent.normalTex;
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


	view.light.data                  = {};
	view.light.data.ambientColor     = float3(0.0f);
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
