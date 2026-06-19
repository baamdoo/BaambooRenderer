#pragma once
#include "Components.h"
#include "SceneRenderView.h"
#include "ModelLoader.h"
#include "RenderGraph.h"

#include <atomic>

namespace baamboo
{

inline constexpr u32 kTerrainLodStatsDepths = 8u;

class RenderGraph;
class EditorCamera;
class TransformSystem;
class StaticMeshSystem;
class MaterialSystem;
class SkyLightSystem;
class LocalLightSystem;
class AtmosphereSystem;
class CloudSystem;
class PostProcessSystem;

// Surface feature requirements — OR-union of what this frame's passes need from SurfaceResolve.
// Lets the resolve skip producing caches that no pass consumes this frame (demand-driven).
enum SurfaceRequirementBits : u32
{
	SURFACE_REQ_NORMAL_ROUGHNESS = 1u << 0, // CoreCache (oct normal + roughness + material class)
	SURFACE_REQ_BASE_COLOR       = 1u << 1, // MaterialCache (baseColor + metalness) — future toggle
	SURFACE_REQ_EMISSIVE         = 1u << 2, // EmissiveCache — future toggle
	SURFACE_REQ_UV_GRAD          = 1u << 3, // DerivativeCache — future toggle
	SURFACE_REQ_ALL              = 0xFFFFFFFFu,
};

struct FrameData
{
	u64 componentMarker = 0;

	// MRT
	Weak< render::RenderTarget > pPhase2Draw;

	// AtmosphereLUTs
	Weak< render::Texture > pTransmittanceLUT;
	Weak< render::Texture > pMultiScatteringLUT;
	Weak< render::Texture > pSkyViewLUT;
	Weak< render::Texture > pAerialPerspectiveLUT;
	Weak< render::Texture > pAtmosphereAmbientLUT;
	//
	Weak< render::Texture > pSkyboxLUT;
	Weak< render::Texture > pVolumetricFogLUT;

	// CloudLUTs
	Weak< render::Texture > pCloudWeatherMap;
	Weak< render::Texture > pCloudProfileLUT;
	Weak< render::Texture > pCloudBaseNoiseLUT;
	Weak< render::Texture > pCloudShadowMap;
	Weak< render::Texture > pCloudScatteringLUT;

	// Visibility buffer (thin geometry pass) + resolved feature caches.
	//   pVBuf0        R32_UINT   : bit31 valid | bit30 terrain | lod:24-26 | instanceID:0-23; sky = 0 (clear)
	//   pVBuf1        R32_UINT   : (meshletIndex:25 | triLocal:7)
	//   pVelocity     RG16F      : currUV - prevUV (screen-space motion)
	//   pCoreNormal   RG16_SNORM : signed octahedral world-space normal (.xy)
	//   pCoreMaterial RGBA8_UNORM: R linear roughness | G material class | BA spare
	Weak< render::Texture > pVBuf0;
	Weak< render::Texture > pVBuf1;
	Weak< render::Texture > pVelocity;
	Weak< render::Texture > pCoreNormal;
	Weak< render::Texture > pCoreMaterial;

	// Scene buffers (Color + depth + HiZ)
	Weak< render::Texture > pColor;
	Weak< render::Texture > pDepth;
	Weak< render::Texture > pHiZ;

	// Terrain SurfaceResolve inputs
	Weak< render::Texture > pHeightmap;
	Weak< render::Buffer >  pTerrainPatches;
	std::function< TerrainParams() > pGetTerrainParams; // callback to query the current terrain's parameters (for SurfaceResolve)

	// Light culling buffers
	Weak< render::Buffer > pClusterAABBBuffer;
	Weak< render::Buffer > pLightGridBuffer;
	Weak< render::Buffer > pLightListDataBuffer;

	// samplers
	Arc< render::Sampler > pPointClamp;
	Arc< render::Sampler > pPointWrap;
	Arc< render::Sampler > pLinearClamp;
	Arc< render::Sampler > pLinearWrap;
	Arc< render::Sampler > pPointClampMin;     // POINT + MIN reduction
	Arc< render::Sampler > pLinearClampMin;    // LINEAR + MIN reduction for HiZ occlusion testing
	Arc< render::Sampler > pPointClampNearest; // POINT + NEAREST mip — valid for integer textures

	u32 totalInstances          = 0; // this frame's total instance count (not stale)
	u32 phase1InstanceDrawCount = 0; // instances emitted by Phase1 cull (last-frame-visible survivors)
	u32 phase2InstanceDrawCount = 0; // instances emitted by Phase2 cull (newly visible after HZB)

#if PROFILING_LEVEL >= 1
	// Per-phase meshlet/triangle counters populated by GBufferNode atomics in the task shader.
	//   meshletTotal       — meshlet candidates examined by task shader (post-LOD, per emitted instance).
	//   meshletDrawn       — meshlets that passed every task-shader cull and survived to launch a mesh shader workgroup.
	//   triangleCandidates — triangles that ENTER the mesh shader.
	u32 phase1MeshletTotal       = 0;
	u32 phase2MeshletTotal       = 0;
	u32 phase1MeshletDrawn       = 0;
	u32 phase2MeshletDrawn       = 0;
	u32 phase1TriangleCandidates = 0;
	u32 phase2TriangleCandidates = 0;
#endif

	// --- Terrain cull stats (TerrainPatchCullingCS) ---
	//   totalPatches        — quadtree node count this frame (= TerrainQuadtree::NumNodes()).
	//   phase1DrawCount     — patches emitted by Phase 1 terrain cull (prev-visible survivors).
	//   phase2DrawCount     — patches emitted by Phase 2 terrain cull (newly visible after HZB).
	//   phase{1,2}Triangles — drawCount × per-patch tris (surface 2*(N-1)² + skirt 8*(N-1))
	u32 terrainTotalPatches    = 0;
	u32 terrainPhase1DrawCount = 0;
	u32 terrainPhase2DrawCount = 0;
	u32 terrainPhase1Triangles = 0;
	u32 terrainPhase2Triangles = 0;
#if PROFILING_LEVEL >= 1
	u32 terrainPhase1LodPatches[kTerrainLodStatsDepths] = {};
	u32 terrainPhase2LodPatches[kTerrainLodStatsDepths] = {};
#endif

	// Runtime culling toggles — bitmask consumed by task + mesh shaders per-frame as push constant.
	// Bits:
	//   bit 0 : per-triangle backface cull
	//   bit 1 : per-triangle sub-pixel cull
	//   bit 2 : per-meshlet frustum cull
	//   bit 3 : per-meshlet backface cone cull
	//   bit 4 : per-meshlet occlusion (2-pass HiZ + visibility persistence)
	u32 cullFlags = 0x1F; // all five toggles on by default

	float sseThresholdPx = 1.0f; // for lod selection

	// Demand-driven SurfaceResolve gating
	u32 surfaceRequirements = SURFACE_REQ_ALL;
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
	void RemoveRenderNode(const std::string& nodeName);

	void Update(float dt, const EditorCamera& edCamera);

	void OnWindowResized(u32 width, u32 height);

	[[nodiscard]]
	SceneRenderView RenderView(const EditorCamera& edCamera, float2 viewport, u64 frame, const render::DeviceSettings& ds) const;

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

	const std::vector< Arc< render::RenderNode > >& GetRenderNodes() const { return m_RenderGraph.GetRenderNodes(); }
	const Arc< render::RenderNode >& GetRenderNodeByName(const std::string& nodeName) const { return m_RenderGraph.GetRenderNodeByName(nodeName); }

	
	void SetCameraFreezeRequest(bool bFreeze) { m_CameraFreezeRequest.store(bFreeze); }
	bool GetCameraFreezeRequest() const { return m_CameraFreezeRequest.load(); }
	bool IsCameraFrozen() const { return m_bCameraFrozen; }
	u64  GetFrozenAtFrame() const { return m_FrozenAtFrame; }

	void SetDebugClusterWireframe(bool b) { m_DebugShowCluster.store(b); }
	bool GetDebugClusterWireframe() const { return m_DebugShowCluster.load(); }
	void SetDebugClusterHeatmap(bool b) { m_DebugClusterHeatmap.store(b); }
	bool GetDebugClusterHeatmap() const { return m_DebugClusterHeatmap.load(); }
	void SetDebugSkipEmpty(bool b) { m_DebugSkipEmpty.store(b); }
	bool GetDebugSkipEmpty() const { return m_DebugSkipEmpty.load(); }
	void SetDebugSaturationMax(u32 v) { m_DebugSaturationMax.store(v); }
	u32  GetDebugSaturationMax() const { return m_DebugSaturationMax.load(); }
	void SetDebugLightTypeMask(u32 m) { m_DebugLightTypeMask.store(m); }
	u32  GetDebugLightTypeMask() const { return m_DebugLightTypeMask.load(); }
	void SetDebugSurfaceView(u32 v) { m_DebugSurfaceView.store(v); }
	u32  GetDebugSurfaceView() const { return m_DebugSurfaceView.load(); }
	void SetDebugLines(std::vector< DebugLineVertex >&& lines);
	void SetDebugLinesAlreadyLocked(std::vector< DebugLineVertex >&& lines);
	void ClearDebugLines();
	void ClearDebugLinesAlreadyLocked();

	const MeshData* GetMeshData(u32 meshID) const { auto it = m_MeshData.find(meshID); return (it != m_MeshData.end()) ? &it->second : nullptr;  }
	const Skeleton* GetSkeleton(u32 skeletonID) const { auto it = m_Skeletons.find(skeletonID); return (it != m_Skeletons.end()) ? &it->second : nullptr; }
	const AnimationClip* GetAnimationClip(u32 clipID) const { auto it = m_AnimationClips.find(clipID); return (it != m_AnimationClips.end()) ? &it->second : nullptr; }

	u32 GetSkeletonCount() const { return static_cast<u32>(m_Skeletons.size()); }
	u32 GetAnimationClipCount() const { return static_cast<u32>(m_AnimationClips.size()); }

private:
	u32 StoreMeshData(const MeshData& meshData);
	u32 StoreSkeletonData(const Skeleton& skeleton);
	u32 StoreAnimationClip(const AnimationClip& clip);

	void OnEntityRemoved(Entity entity);

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
	SkyLightSystem*    m_pSkyLightSystem    = nullptr;
	LocalLightSystem*  m_pLocalLightSystem  = nullptr;
	AtmosphereSystem*  m_pAtmosphereSystem  = nullptr;
	CloudSystem*       m_pCloudSystem       = nullptr;
	PostProcessSystem* m_pPostProcessSystem = nullptr;

	RenderGraph m_RenderGraph;

	bool m_bDirtyMarks = false;

	// animations
	std::unordered_map< u32, MeshData >      m_MeshData;
	std::unordered_map< u32, Skeleton >      m_Skeletons;
	std::unordered_map< u32, AnimationClip > m_AnimationClips;

	std::unordered_map< std::string, ModelLoader* > m_ModelLoaderCache;

	mutable std::mutex m_SceneMutex;

	std::atomic< bool >      m_CameraFreezeRequest{ false };
	mutable bool             m_bCameraFrozen   = false;
	mutable CameraRenderView m_FrozenCamera    = {};
	mutable float2           m_FrozenViewport  = float2(0.0f, 0.0f);
	mutable u64              m_FrozenAtFrame   = 0;

	std::atomic< bool > m_DebugShowCluster{ false };
	std::atomic< bool > m_DebugClusterHeatmap{ false };
	std::atomic< bool > m_DebugSkipEmpty{ true };
	std::atomic< u32 >  m_DebugSaturationMax{ 16u};
	std::atomic< u32 >  m_DebugLightTypeMask{ 0u };
	std::atomic< u32 >  m_DebugSurfaceView{ 0u };
	std::vector< DebugLineVertex > m_DebugLines;
	u64 m_DebugLinesVersion = 0u;
};

} // namespace baamboo
