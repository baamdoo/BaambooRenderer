#pragma once
#include "Components.h"
#include "SceneRenderView.h"
#include "ModelLoader.h"
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

class EditorCamera;
class TransformSystem;
class StaticMeshSystem;
class MaterialSystem;
class SkyLightSystem;
class LocalLightSystem;
class AtmosphereSystem;
class CloudSystem;
class PostProcessSystem;

struct FrameData
{
	float time  = 0.0f;
	u64   frame = 0;

	u64 componentMarker = 0;

	bool bSkyboxMode = false;

	CameraData camera = {};

	// AtmosphereLUTs
	Weak< render::Texture > pTransmittanceLUT;
	Weak< render::Texture > pMultiScatteringLUT;
	Weak< render::Texture > pSkyViewLUT;
	Weak< render::Texture > pAerialPerspectiveLUT;
	Weak< render::Texture > pAtmosphereAmbientLUT;
	//
	Weak< render::Texture > pSkyboxLUT;

	// CloudLUTs
	Weak< render::Texture > pCloudScatteringLUT;

	// Scene buffers
	Weak< render::Texture > pGBuffer0;
	Weak< render::Texture > pGBuffer1;
	Weak< render::Texture > pGBuffer2;
	Weak< render::Texture > pGBuffer3;
	Weak< render::Texture > pColor;
	Weak< render::Texture > pDepth;

	// samplers
	Arc< render::Sampler > pPointClamp;
	Arc< render::Sampler > pLinearClamp;
	Arc< render::Sampler > pLinearWrap;
};
inline FrameData g_FrameData = {};

class Scene
{
public:
	Scene(const std::string& name);
	~Scene();

	[[nodiscard]]
	class Entity CreateEntity(const std::string& tag = "Empty");
	void RemoveEntity(Entity entity);

	class Entity ImportModel(const fs::path& filepath, MeshDescriptor descriptor);
	class Entity ImportModel(Entity rootEntity, const fs::path& filepath, MeshDescriptor descriptor);

	void AddRenderNode(Arc< render::RenderNode > pNode);

	void Update(float dt);

	void OnWindowResized(u32 width, u32 height);

	[[nodiscard]]
	SceneRenderView RenderView(const EditorCamera& edCamera, bool bDrawUI) const;

	[[nodiscard]]
	const std::string& Name() const { return m_Name; }
	[[nodiscard]]
	bool IsLoading() const { return m_bLoading; }

	[[nodiscard]]
	entt::registry& Registry() { return m_Registry; }
	[[nodiscard]]
	const entt::registry& Registry() const { return m_Registry; }
	[[nodiscard]]
	TransformSystem* GetTransformSystem() const { return m_pTransformSystem; }

	std::vector< Arc< render::RenderNode > >& GetRenderNodes() { return m_RenderNodes; }

	const MeshData* GetMeshData(u32 meshID) const { auto it = m_MeshData.find(meshID); return (it != m_MeshData.end()) ? &it->second : nullptr;  }
	const Skeleton* GetSkeleton(u32 skeletonID) const { auto it = m_Skeletons.find(skeletonID); return (it != m_Skeletons.end()) ? &it->second : nullptr; }
	const AnimationClip* GetAnimationClip(u32 clipID) const { auto it = m_AnimationClips.find(clipID); return (it != m_AnimationClips.end()) ? &it->second : nullptr; }

	u32 GetSkeletonCount() const { return static_cast<u32>(m_Skeletons.size()); }
	u32 GetAnimationClipCount() const { return static_cast<u32>(m_AnimationClips.size()); }

private:
	u32 StoreMeshData(const MeshData& meshData);
	u32 StoreSkeletonData(const Skeleton& skeleton);
	u32 StoreAnimationClip(const AnimationClip& clip);

private:
	friend class Entity;
	entt::registry m_Registry;

	std::string m_Name;
	bool m_bLoading = false;

	// [entity, dirty-components]
	mutable std::unordered_map< u64, u64 > m_EntityDirtyMasks;

	// systems
	TransformSystem*   m_pTransformSystem   = nullptr;
	StaticMeshSystem*  m_pStaticMeshSystem  = nullptr;
	MaterialSystem*    m_pMaterialSystem    = nullptr;
	SkyLightSystem*    m_pSkyLightSystem    = nullptr;
	LocalLightSystem*  m_pLocalLightSystem  = nullptr;
	AtmosphereSystem*  m_pAtmosphereSystem  = nullptr;
	CloudSystem*       m_pCloudSystem       = nullptr;
	PostProcessSystem* m_pPostProcessSystem = nullptr;

	std::vector< Arc< render::RenderNode > > m_RenderNodes;

	// animations
	std::unordered_map< u32, MeshData >      m_MeshData;
	std::unordered_map< u32, Skeleton >      m_Skeletons;
	std::unordered_map< u32, AnimationClip > m_AnimationClips;

	std::unordered_map< std::string, ModelLoader* > m_ModelLoaderCache;

	mutable std::mutex m_SceneMutex;
};

} // namespace baamboo 