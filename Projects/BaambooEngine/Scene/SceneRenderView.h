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

	std::string_view   geometry;
	struct MaterialRenderView
	{
		float3 tint{ 1, 1, 1 };

		std::string_view albedo;
		std::string_view normal;
		std::string_view specular;
		std::string_view emission;
		std::string_view orm;
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