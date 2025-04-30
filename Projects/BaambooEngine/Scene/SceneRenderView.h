#pragma once
#include "BaambooCore/Common.h"

namespace baamboo
{

struct TransformRenderView
{
	u32 id;

	mat4 mWorld;
};

struct StaticMeshRenderView
{
	u32 id;

	struct GeometryRenderView
	{
		u32 vb;
		u32 vOffset;
		u32 vCount;

		u32 ib;
		u32 iOffset;
		u32 iCount;
	} geometry;

	struct MaterialRenderView
	{
		float3 tint{ 1, 1, 1 };

		u32 albedo;
		u32 normal;
		u32 specular;
		u32 ao;
		u32 roughness;
		u32 metallic;
		u32 emission;
	} material;
};

struct CameraRenderView
{
	u32 id;

	mat4 mView;
	mat4 mProj;
	float3 pos;
};

struct DrawData
{
	u32 transform;
	u32 camera;
	u32 mesh;
};

//-------------------------------------------------------------------------
// SceneRenderView : Holds all the scene data
//                   required for rendering in a refined state.
//-------------------------------------------------------------------------
struct SceneRenderView
{
	std::vector< TransformRenderView >  transforms;
	std::vector< CameraRenderView >     cameras;
	std::vector< StaticMeshRenderView > meshes;

	std::unordered_map< u32, DrawData > draws;
};

} // namespace baamboo