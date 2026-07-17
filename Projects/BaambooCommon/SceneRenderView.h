#pragma once
#include "Boundings.h"
#include "ShaderTypes.h"
#include "EngineTypes.h"
#include "ComponentTypes.h"
#include "RenderCommon/RenderNode.h"

#include <mutex>
#include <functional>

enum eComponentType
{
	CTransform   = 0,
	CStaticMesh  = 1,
	CDynamicMesh = 2,
	CMaterial    = 3,
	CSkyLight    = 4,
	CAtmosphere  = 5,
	CCloud       = 6,
	CLocalLight   = 7,
	CPostProcess  = 8,
	CVoxelTerrain = 9,

	// ...
	NumComponents
};

struct TransformRenderView
{
	u64 id;

	mat4 mWorld;
	mat4 mWorldInverse;
};

struct CameraRenderView
{
	mat4 mView;
	mat4 mProj;

	float3 pos;
	float  zNear;
	float  zFar;

	float maxVisibleDistance;
};

struct StaticMeshRenderView
{
	u64         id;
	std::string tag;

	BoundingBox    aabb;
	BoundingSphere sphere;

	const void* vData;
	u32         vCount;

	struct
	{
		const void* iData;
		u32         iCount;

		const void* mData;
		u32         mCount;
		const void* mvData;
		u32         mvCount;
		const void* mtData;
		u32         mtCount;

		float simplifyError; // for lod scale
	} lods[LOD_COUNT];
	u8 maxLOD;
};

struct MaterialRenderView
{
	u64 id;

	float3 tint;
	float  shininess;
	float  metallic;
	float  roughness;
	float  ior;

	float3 emissionColor = float3(1.0f);
	float  emissivePower;

	float  alphaCutoff;
	float  clearcoat;
	float  clearcoatRoughness;
	float  anisotropy;
	float  anisotropyRotation;

	float3 specularColor;
	float  specularStrength;
	float3 sheenColor;
	float  sheenRoughness;

	float  subsurface;
	float  transmission;
	u32    materialType = 0u;
	u32    materialFlags = 0u;

	std::string albedoTex;
	std::string normalTex;
	std::string specularTex;
	std::string aoTex;
	std::string roughnessTex;
	std::string metallicTex;
	std::string emissionTex;
	std::string clearcoatTex;
	std::string sheenTex;
	std::string anisotropyTex;
	std::string subsurfaceTex;
	std::string transmissionTex;
};

struct DrawRenderView
{
	u32 transform = kInvalidIndex;
	u32 mesh      = kInvalidIndex;
	u32 material  = kInvalidIndex;
};

using LightRenderView = LightData;

struct AtmosphereRenderView
{
	u64            id;
	AtmosphereData data;

	u32 msIsoSampleCount;
	u32 msNumRaySteps;
	u32 svMinRaySteps;
	u32 svMaxRaySteps;

	u32   numFogRaymarchSteps;
	float volumetricFogDistanceMeter;

	std::string skybox;
};

struct CloudRenderView
{
	u64             id;
	CloudData       data;
	CloudShadowData shadow;

	eCloudUprezRatio uprezRatio;

	u32 numCloudRaymarchSteps;
	u32 numLightRaymarchSteps;
	float frontDepthBias;
	float temporalBlendAlpha;

	std::string blueNoiseTex;
	std::string weatherMap;
	std::string curlNoiseTex;
};

struct PostProcessRenderView
{
	u64 id;
	u64 effectBits;

	// height fog (TODO)
	struct
	{
		float exponentialFactor;
	} heightFog;

	// bloom (TODO)
	struct
	{
		i32 filterSize;
	} bloom;

	// anti-aliasing
	struct
	{
		eAntiAliasingType type;

		// TAA
		float blendFactor;
		float sharpness;
	} aa;

	// tone-mapping
	struct
	{
		eToneMappingOp op;

		float ev100;
		float gamma;
	} tonemap;
};

struct VoxelTerrainRenderView
{
	bool   bValid   = false;
	u32    revision = 0u;

	float3 originWorld         = float3(0.0f);
	float  chunkWorldSizeMeter = 64.0f;

	VoxelTerrainGenParams genParams = {};

	VoxelDiceSettings dice;
};

struct DebugRenderView
{
	u64 id;
	u64 effectBits;
};

struct DebugViewFlags
{
	bool bShowClusterWireframe = false;
	bool bClusterHeatmap       = false;
	bool bSkipEmptyClusters    = true;
	u32  saturationMax         = 16u;

	u32 lightTypeMask = 0u;

	u32 surfaceDebugView = 0u;
};

struct DebugLineVertex
{
	float3 position = float3(0.0f);
	float  pad0     = 0.0f;
	float3 color    = float3(1.0f);
	float  alpha    = 1.0f;
};

//-------------------------------------------------------------------------
// SceneRenderView : Holds all the scene data
//                   required for rendering in a refined state.
//-------------------------------------------------------------------------
struct SceneRenderView
{
	float time;
	u64   frame;

	float2 viewport;

	float sseThresholdPx; // for lod selection

	u32 cullFlags;
	u32 hiZMipCount;
	u32 hiZWidth;
	u32 hiZHeight;

	std::vector< TransformRenderView >  transforms;
	std::vector< StaticMeshRenderView > meshes; 
	std::vector< MaterialRenderView >   materials;

	std::unordered_map< u32, DrawRenderView > draws;

	CameraRenderView      camera;
	LightRenderView       light;
	AtmosphereRenderView  atmosphere;
	CloudRenderView       cloud;
	PostProcessRenderView postProcess;
	DebugRenderView       debug;

	VoxelTerrainRenderView voxelTerrain;

	bool             bFrozen = false;
	CameraRenderView frozenCamera = {};
	float2           frozenViewport = float2(0.0f, 0.0f); // viewport (px) at freeze moment

	DebugViewFlags debugFlags = {};
	std::vector< DebugLineVertex > debugLines;
	u64 debugLinesVersion = 0u;

	// for sync producer(SceneRenderView)-consumer(Renderer)
	std::mutex* pSceneMutex;

	std::unordered_map< u64, u64 >* pEntityDirtyMarks;
};
