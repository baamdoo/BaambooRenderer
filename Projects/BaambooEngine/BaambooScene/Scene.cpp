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
#include "Systems/VoxelTerrainSystem.h"
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
	m_pVoxelTerrainSystem = new VoxelTerrainSystem(m_Registry);
	m_pStaticMeshSystem  = new StaticMeshSystem(m_Registry);
	m_pSkyLightSystem    = new SkyLightSystem(m_Registry, m_pTransformSystem);
	m_pAtmosphereSystem  = new AtmosphereSystem(m_Registry, m_pSkyLightSystem);
	m_pCloudSystem       = new CloudSystem(m_Registry, m_pAtmosphereSystem);
	m_pLocalLightSystem  = new LocalLightSystem(m_Registry, m_pTransformSystem);
	m_pPostProcessSystem = new PostProcessSystem(m_Registry);
}

Scene::~Scene()
{
	// cached entities point into this scene's registry; a later scene (e.g. after a
	// runtime renderer-API switch) must not resurrect them via the import cache
	s_ModelCache.clear();

	for (auto& [_, pLoader] : m_ModelLoaderCache)
		RELEASE(pLoader);

	RELEASE(m_pPostProcessSystem);
	RELEASE(m_pVoxelTerrainSystem);
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

	return newEntity;
}

void Scene::RemoveEntity(Entity entity)
{
	if (!entity.IsValid())
		return;

	printf("Remove entity%d\n", entity.id());

	if (entity.GetComponent< TransformComponent >().hierarchy.parent != entt::null)
		m_pTransformSystem->DetachChild(entity.ID());

	entt::entity child = entity.GetComponent< TransformComponent >().hierarchy.firstChild;
	while (child != entt::null)
	{
		auto childEntity = Entity(this, child);
		const entt::entity nextSibling = childEntity.GetComponent< TransformComponent >().hierarchy.nextSibling;

		RemoveEntity(childEntity);
		child = nextSibling;
	}

	//OnEntityRemoved(entity);

	m_Registry.destroy(entity.ID());
}

Entity Scene::ImportModel(const fs::path& filepath, MeshDescriptor descriptor)
{
	return ImportModel(Entity{}, filepath, descriptor);
}

Entity Scene::ImportModel(Entity parentEntity, const fs::path& filepath, MeshDescriptor descriptor)
{
	if (auto it = s_ModelCache.find(filepath.string()); it != s_ModelCache.end())
	{
		return it->second.Clone();
	}

	m_bLoading = true;

	auto pLoader   = new ModelLoader(filepath, descriptor);
	auto pRootNode = pLoader->GetRootNode();
	if (!pRootNode)
	{
		RELEASE(pLoader);
		m_bLoading = false;
		return {};
	}

	m_ModelLoaderCache.emplace(filepath.string(), pLoader);

	std::string parentPath = filepath.parent_path().string() + "/";
	std::function< Entity(const ModelNode*, Entity) > ProcessNode = [&](const ModelNode* node, Entity parent)
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
			quat   rotation;
			glm::decompose(node->mTransform, scale, rotation, translation, skew, perspective);

			transformComponent.transform.position = translation;
			transformComponent.transform.rotation = glm::eulerAngles(rotation);
			transformComponent.transform.scale    = scale;

			// Process meshes
			const bool bSplitMeshes = node->meshIndices.size() > 1;
			for (size_t meshOrdinal = 0; meshOrdinal < node->meshIndices.size(); ++meshOrdinal)
			{
				const u32 meshIndex = node->meshIndices[meshOrdinal];
				const MeshData& meshData = pLoader->GetMeshes()[meshIndex];

				Entity meshEntity = entity;
				if (bSplitMeshes)
				{
					const std::string meshName = meshData.name.empty()
						? node->name + "_Mesh_" + std::to_string(meshOrdinal)
						: meshData.name;
					meshEntity = CreateEntity(meshName);
					entity.AttachChild(meshEntity.ID());
				}

				{
					// static mesh
					auto& meshComponent = meshEntity.AttachComponent< StaticMeshComponent >();
					meshComponent.tag  = meshData.name;
					meshComponent.path = filepath.string();

					meshComponent.aabb   = meshData.aabb;
					meshComponent.sphere = BoundingSphere(meshData.aabb);

					meshComponent.numVertices = static_cast<u32>(meshData.vertices.size());
					meshComponent.pVertices   = const_cast<Vertex*>(meshData.vertices.data());

					meshComponent.maxLOD = static_cast<u8>(meshData.lods.size() - 1);
					for (u8 i = 0; i <= meshComponent.maxLOD; i++)
					{
						meshComponent.lods[i].numIndices = static_cast<u32>(meshData.lods[i].indices.size());
						if (meshComponent.lods[i].numIndices > 0)
							meshComponent.lods[i].pIndices = const_cast<Index*>(meshData.lods[i].indices.data());

						meshComponent.lods[i].numMeshlets = static_cast<u32>(meshData.lods[i].meshlets.size());
						if (meshComponent.lods[i].numMeshlets > 0)
						{
							meshComponent.lods[i].pMeshlets = const_cast<Meshlet*>(meshData.lods[i].meshlets.data());

							meshComponent.lods[i].numMeshletVertices = static_cast<u32>(meshData.lods[i].meshletVertices.size());
							meshComponent.lods[i].pMeshletVertices   = const_cast<u32*>(meshData.lods[i].meshletVertices.data());

							meshComponent.lods[i].numMeshletTriangles = static_cast<u32>(meshData.lods[i].meshletTriangles.size());
							meshComponent.lods[i].pMeshletTriangles   = const_cast<u32*>(meshData.lods[i].meshletTriangles.data());
						}

						meshComponent.lods[i].simplifyError = meshData.lods[i].simplifyError;
					}
				}

				// Material
				auto& material = meshEntity.AttachComponent< MaterialComponent >();
				if (meshData.materialIndex < pLoader->GetMaterials().size())
				{
					const MaterialData& matData = pLoader->GetMaterials()[meshData.materialIndex];

					material.name = matData.name;

					material.tint               = float4(matData.diffuse, 1.0f);
					material.metallic           = matData.metallic;
					material.roughness          = matData.roughness;
					material.ior                = matData.ior;
					material.clearcoat          = matData.clearcoat;
					material.clearcoatRoughness = matData.clearcoatRoughness;
					material.anisotropy         = matData.anisotropy;
					material.anisotropyRotation = matData.anisotropyRotation;
					material.specularColor      = matData.specularColor;
					material.specularStrength   = matData.specularStrength;
					material.sheenColor         = matData.sheenColor;
					material.sheenRoughness     = matData.sheenRoughness;
					material.transmission       = matData.transmission;

					auto resolveTexPath = [&](const std::string& path) -> std::string
					{
						if (path.empty()) return "";
						return parentPath + path;
					};

					material.albedoTex       = resolveTexPath(matData.albedoPath);
					material.normalTex       = resolveTexPath(matData.normalPath);
					material.metallicTex     = resolveTexPath(matData.metallicPath);
					material.roughnessTex    = resolveTexPath(matData.roughnessPath);
					material.aoTex           = resolveTexPath(matData.aoPath);
					material.emissionTex     = resolveTexPath(matData.emissivePath);
					material.clearcoatTex    = resolveTexPath(matData.clearcoatPath);
					material.sheenTex        = resolveTexPath(matData.sheenPath);
					material.anisotropyTex   = resolveTexPath(matData.anisotropyPath);
					material.transmissionTex = resolveTexPath(matData.transmissionPath);
				}
			}

			// Process children
			for (const auto& pChild : node->pChilds)
			{
				ProcessNode(pChild, entity);
			}

			return entity;
		};

	Entity rootEntity = ProcessNode(pRootNode, parentEntity);

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

void Scene::OnEntityRemoved(Entity entity)
{
	Registry().patch< TransformComponent >(entity.ID(), [](auto&) {});
	if (entity.HasAny< StaticMeshComponent >())
	{
		Registry().patch< StaticMeshComponent >(entity.ID(), [](auto&) {});
	}
	if (entity.HasAny< MaterialComponent >())
	{
		Registry().patch< MaterialComponent >(entity.ID(), [](auto&) {});
	}
	if (entity.HasAny< LightComponent >())
	{
		Registry().patch< LightComponent >(entity.ID(), [](auto&) {});
	}
	if (entity.HasAny< AtmosphereComponent >())
	{
		Registry().patch< AtmosphereComponent >(entity.ID(), [](auto&) {});
	}
	if (entity.HasAny< CloudComponent >())
	{
		Registry().patch< CloudComponent >(entity.ID(), [](auto&) {});
	}
}

void Scene::Update(f32 dt, const EditorCamera& edCamera)
{
	std::lock_guard< std::mutex > lock(m_SceneMutex);

	s_SceneRunningTime += dt;
	u64 changedComponents = 0;
	const auto updateSystem = [&]< typename TSystem >(TSystem* pSystem, eComponentType component)
	{
		const bool bChanged = pSystem->HasPendingRenderDataChanges();
		pSystem->UpdateRenderData(edCamera);
		if (bChanged)
			changedComponents |= 1ULL << component;
	};

	updateSystem(m_pTransformSystem, eComponentType::CTransform);

	const bool bStaticMeshChanged = m_pStaticMeshSystem->HasPendingRenderDataChanges();
	m_pStaticMeshSystem->UpdateRenderData(edCamera);
	if (bStaticMeshChanged)
	{
		changedComponents |= 1ULL << eComponentType::CStaticMesh;
		changedComponents |= 1ULL << eComponentType::CMaterial;
	}

	updateSystem(m_pSkyLightSystem, eComponentType::CSkyLight);
	updateSystem(m_pAtmosphereSystem, eComponentType::CAtmosphere);
	updateSystem(m_pCloudSystem, eComponentType::CCloud);
	updateSystem(m_pLocalLightSystem, eComponentType::CLocalLight);
	updateSystem(m_pPostProcessSystem, eComponentType::CPostProcess);
	updateSystem(m_pVoxelTerrainSystem, eComponentType::CVoxelTerrain);

	if (changedComponents != 0)
	{
		++m_SceneRevision;
		for (u32 component = 0; component < eComponentType::NumComponents; ++component)
		{
			if ((changedComponents & (1ULL << component)) != 0)
				++m_ComponentRevisions[component];
		}
	}
}

void Scene::OnWindowResized(u32 width, u32 height)
{
	for (auto node : m_RenderGraph.GetRenderNodes())
		if (node)
			node->Resize(width, height);
}

void Scene::SetDebugLines(std::vector< DebugLineVertex >&& lines)
{
	std::lock_guard< std::mutex > lock(m_SceneMutex);
	SetDebugLinesAlreadyLocked(std::move(lines));
}

void Scene::SetDebugLinesAlreadyLocked(std::vector< DebugLineVertex >&& lines)
{
	m_DebugLines = std::move(lines);
	++m_DebugLinesVersion;
}

void Scene::ClearDebugLines()
{
	std::lock_guard< std::mutex > lock(m_SceneMutex);
	ClearDebugLinesAlreadyLocked();
}

void Scene::ClearDebugLinesAlreadyLocked()
{
	m_DebugLines.clear();
	++m_DebugLinesVersion;
}

SceneRenderView Scene::RenderView(const EditorCamera& edCamera, float2 viewport, u64 producerSequence, const render::DeviceSettings& ds) const
{
	std::lock_guard< std::mutex > lock(m_SceneMutex);

	SceneRenderView view = {};
	view.time               = s_SceneRunningTime;
	view.producerSequence   = producerSequence;
	view.sceneRevision      = m_SceneRevision;
	view.componentRevisions = m_ComponentRevisions;
	view.viewport       = viewport;
	view.sseThresholdPx = g_FrameData.sseThresholdPx;
	view.cullFlags      = g_FrameData.cullFlags;

	if (auto pHiZ = g_FrameData.pHiZ.lock())
	{
		view.hiZMipCount = pHiZ->MipLevels();
		view.hiZWidth    = pHiZ->Width();
		view.hiZHeight   = pHiZ->Height();
	}
	else
	{
		view.hiZMipCount = 0u;
		view.hiZWidth    = 0u;
		view.hiZHeight   = 0u;
	}
	view.camera.mView              = edCamera.GetView();
	view.camera.mProj              = edCamera.GetProj();
	view.camera.pos                = edCamera.GetPosition();
	view.camera.zNear              = edCamera.zNear;
	view.camera.zFar               = edCamera.zFar;
	view.camera.maxVisibleDistance = edCamera.maxVisibleDistance;

	const bool bRequest = m_CameraFreezeRequest.load(std::memory_order_relaxed);
	if (bRequest && !m_bCameraFrozen)
	{
		m_FrozenCamera    = view.camera;
		m_FrozenViewport  = viewport;
		m_FrozenAtFrame   = producerSequence;
		m_bCameraFrozen   = true;
	}
	else if (!bRequest && m_bCameraFrozen)
	{
		m_bCameraFrozen = false;
	}

	view.bFrozen        = m_bCameraFrozen;
	view.frozenCamera   = m_bCameraFrozen ? m_FrozenCamera   : view.camera;
	view.frozenViewport = m_bCameraFrozen ? m_FrozenViewport : viewport;

	view.debugFlags.bShowClusterWireframe = m_DebugShowCluster.load(std::memory_order_relaxed);
	view.debugFlags.bClusterHeatmap       = m_DebugClusterHeatmap.load(std::memory_order_relaxed);
	view.debugFlags.bSkipEmptyClusters    = m_DebugSkipEmpty.load(std::memory_order_relaxed);
	view.debugFlags.saturationMax         = m_DebugSaturationMax.load(std::memory_order_relaxed);
	view.debugFlags.lightTypeMask         = m_DebugLightTypeMask.load(std::memory_order_relaxed);
	view.debugFlags.surfaceDebugView      = m_DebugSurfaceView.load(std::memory_order_relaxed);
	view.debugLines = m_DebugLines;
	view.debugLinesVersion = m_DebugLinesVersion;

	m_pTransformSystem->CollectRenderData(view);
	m_pStaticMeshSystem->CollectRenderData(view);
	m_pSkyLightSystem->CollectRenderData(view);
	m_pLocalLightSystem->CollectRenderData(view);
	m_pAtmosphereSystem->CollectRenderData(view);
	m_pCloudSystem->CollectRenderData(view);
	m_pPostProcessSystem->CollectRenderData(view);
	m_pVoxelTerrainSystem->CollectRenderData(view);
	return view;
}

} // namespace baamboo
