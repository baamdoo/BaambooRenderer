#pragma once
#include "ShaderTypes.h"

struct TransformRenderView
{
	u32 id;

	mat4 mWorld;
};

struct CameraRenderView
{
	mat4 mView;
	mat4 mProj;
	float3 pos;
};

struct StaticMeshRenderView
{
	u32              id;
	std::string_view tag;

	const void* vData;
	u32         vCount;

	const void* iData;
	u32         iCount;
};

struct MaterialRenderView
{
	u32 id;

	float3 tint;
	float  roughness;
	float  metallic;

	std::string_view albedoTex;
	std::string_view normalTex;
	std::string_view specularTex;
	std::string_view aoTex;
	std::string_view roughnessTex;
	std::string_view metallicTex;
	std::string_view emissionTex;
};

struct DrawRenderView
{
	u32 transform = INVALID_INDEX;
	u32 mesh      = INVALID_INDEX;
	u32 material  = INVALID_INDEX;
};

struct LightRenderView
{
	LightData data;
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

	CameraRenderView camera;
	LightRenderView  light;
};
