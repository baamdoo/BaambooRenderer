#include "BaambooPch.h"
#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include "Camera.h"
#include "Systems/TransformSystem.h"
#include "Systems/MeshSystem.h"
#include "Systems/LightSystem.h"
#include "Systems/AtmosphereSystem.h"
#include "Systems/CloudSystem.h"
#include "Systems/PostProcessSystem.h"
#include "Utils/Math.hpp"

#include <queue>
#include <glm/gtx/matrix_decompose.hpp>

namespace baamboo
{

static float s_SceneRunningTime = 0.0f;

static std::unordered_map< std::string, Entity > s_ModelCache;

Scene::Scene(const std::string& name)
	: m_Name(name)
{
	m_pTransformSystem   = new TransformSystem(m_Registry);
	m_pStaticMeshSystem  = new StaticMeshSystem(m_Registry);
	m_pSkyLightSystem    = new SkyLightSystem(m_Registry, m_pTransformSystem);
	m_pAtmosphereSystem  = new AtmosphereSystem(m_Registry, m_pSkyLightSystem);
	m_pCloudSystem       = new CloudSystem(m_Registry, m_pAtmosphereSystem);
	m_pLocalLightSystem  = new LocalLightSystem(m_Registry, m_pTransformSystem);
	m_pPostProcessSystem = new PostProcessSystem(m_Registry);
}

Scene::~Scene()
{
	for (auto& [_, pLoader] : m_ModelLoaderCache)
		RELEASE(pLoader);

	RELEASE(m_pPostProcessSystem);
	RELEASE(m_pLocalLightSystem);
	RELEASE(m_pCloudSystem);
	RELEASE(m_pAtmosphereSystem);
	RELEASE(m_pSkyLightSystem);
	RELEASE(m_pStaticMeshSystem);
	RELEASE(m_pTransformSystem);
}

Entity Scene::CreateEntity(const std::string& tag)
{
	Entity newEntity = Entity(this, m_Registry.create());
	newEntity.AttachComponent< TagComponent >(tag);
	newEntity.AttachComponent< TransformComponent >();

	printf("create entity_%d\n", newEntity.id());

	m_EntityDirtyMasks.emplace(std::make_pair(newEntity.id(), 0));
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
	m_EntityDirtyMasks.erase(entity.id());
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

					mesh.aabb   = meshData.aabb;
					mesh.sphere = BoundingSphere(meshData.aabb);

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

void Scene::AddRenderNode(Arc< render::RenderNode > pNode)
{
	m_RenderGraph.AddRenderNode(pNode);
}

void Scene::RemoveRenderNode(const std::string& nodeName)
{
	m_RenderGraph.RemoveRenderNode(nodeName);
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

void Scene::Update(f32 dt, const EditorCamera& edCamera)
{
	std::lock_guard< std::mutex > lock(m_SceneMutex);

	s_SceneRunningTime += dt;

	for (auto entity : m_pTransformSystem->UpdateRenderData(edCamera))
	{
		u64& dirtyMarks = m_EntityDirtyMasks[entity];
		dirtyMarks |= (1 << eComponentType::CTransform);
	}

	for (auto entity : m_pStaticMeshSystem->UpdateRenderData(edCamera))
	{
		u64& dirtyMarks = m_EntityDirtyMasks[entity];
		dirtyMarks |= (1 << eComponentType::CStaticMesh);
	}

	for (auto entity : m_pSkyLightSystem->UpdateRenderData(edCamera))
	{
		u64& dirtyMarks = m_EntityDirtyMasks[entity];
		dirtyMarks |= (1 << eComponentType::CSkyLight);
	}

	for (auto entity : m_pAtmosphereSystem->UpdateRenderData(edCamera))
	{
		u64& dirtyMarks = m_EntityDirtyMasks[entity];
		dirtyMarks |= (1 << eComponentType::CAtmosphere);
	}

	for (auto entity : m_pCloudSystem->UpdateRenderData(edCamera))
	{
		u64& dirtyMarks = m_EntityDirtyMasks[entity];
		dirtyMarks |= (1 << eComponentType::CCloud);
	}

	for (auto entity : m_pLocalLightSystem->UpdateRenderData(edCamera))
	{
		u64& dirtyMarks = m_EntityDirtyMasks[entity];
		dirtyMarks |= (1 << eComponentType::CLocalLight);
	}

	for (auto entity : m_pPostProcessSystem->UpdateRenderData(edCamera))
	{
		u64& dirtyMarks = m_EntityDirtyMasks[entity];
		dirtyMarks |= (1 << eComponentType::CPostProcess);
	}
}

void Scene::OnWindowResized(u32 width, u32 height)
{
	for (auto node : m_RenderGraph.GetRenderNodes())
		if (node)
			node->Resize(width, height);
}

SceneRenderView Scene::RenderView(const EditorCamera& edCamera, float2 viewport, u64 frame, bool bDrawUI) const
{
	bool bMarkedAny = false;
	for (const auto& pair : m_EntityDirtyMasks)
	{
		bMarkedAny |= (pair.second > 0);
	}

	SceneRenderView view = {};
	view.time     = s_SceneRunningTime;
	view.frame    = frame;
	view.viewport = viewport;
	view.bDrawUI  = bDrawUI;

	view.rg = m_RenderGraph.GetRenderNodes();

	view.pSceneMutex       = &m_SceneMutex;
	view.pEntityDirtyMarks = bMarkedAny ? &m_EntityDirtyMasks : nullptr;

	view.camera.mView              = edCamera.GetView();
	view.camera.mProj              = edCamera.GetProj();
	view.camera.pos                = edCamera.GetPosition();
	view.camera.zNear              = edCamera.zNear;
	view.camera.zFar               = edCamera.zFar;
	view.camera.maxVisibleDistance = edCamera.maxVisibleDistance;

	m_pTransformSystem->CollectRenderData(view);
	m_pStaticMeshSystem->CollectRenderData(view);
	m_pSkyLightSystem->CollectRenderData(view);
	m_pLocalLightSystem->CollectRenderData(view);
	m_pAtmosphereSystem->CollectRenderData(view);
	m_pCloudSystem->CollectRenderData(view);
	m_pPostProcessSystem->CollectRenderData(view);
	return view;
}

} // namespace baamboo
