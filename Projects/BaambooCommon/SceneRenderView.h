#pragma once
#include "ShaderTypes.h"
#include "ComponentTypes.h"

enum eComponentType
{
	CTransform   = 0,
	CStaticMesh  = 1,
	CDynamicMesh = 2,
	CMaterial    = 3,
	CLight       = 4,
	CAtmosphere  = 5,
	CPostProcess = 6,

	// ...
	NumComponents
};

struct TransformRenderView
{
	u64 id;

	mat4 mWorld;
};

struct CameraRenderView
{
	mat4 mView;
	mat4 mProj;

	float3 pos;
	float  zNear;
	float  zFar;
};

struct StaticMeshRenderView
{
	u64         id;
	std::string tag;

	const void* vData;
	u32         vCount;

	const void* iData;
	u32         iCount;
};

struct MaterialRenderView
{
	u64 id;

	float3 tint;
	float3 ambient;

	float shininess;
	float metallic;
	float roughness;
	float ior;
	float emissivePower;

	std::string albedoTex;
	std::string normalTex;
	std::string aoTex;
	std::string roughnessTex;
	std::string metallicTex;
	std::string emissionTex;
};

struct DrawRenderView
{
	u32 transform = INVALID_INDEX;
	u32 mesh      = INVALID_INDEX;
	u32 material  = INVALID_INDEX;
};

using LightRenderView = LightData;

struct AtmosphereRenderView
{
	u64            id;
	AtmosphereData data;

	u32  msIsoSampleCount;
	u32  msNumRaySteps;
	u32  svMinRaySteps;
	u32  svMaxRaySteps;
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

		float gamma;
	} tonemap;
};

//-------------------------------------------------------------------------
// SceneRenderView : Holds all the scene data
//                   required for rendering in a refined state.
//-------------------------------------------------------------------------
struct SceneRenderView
{
	std::vector< TransformRenderView >  transforms;
	std::vector< StaticMeshRenderView > meshes; 
	std::vector< MaterialRenderView >   materials;

	std::unordered_map< u32, DrawRenderView > draws;

	CameraRenderView      camera;
	LightRenderView       light;
	AtmosphereRenderView  atmosphere;
	PostProcessRenderView postProcess;

	// for sync producer(SceneRenderView)-consumer(Renderer)
	std::mutex* pSceneMutex;

	std::unordered_map< u64, u64 >* pEntityDirtyMarks;
};
