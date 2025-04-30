#pragma once
#include "BaambooCore/BackendAPI.h"
#include "BaambooCore/Transform.hpp"
#include "BaambooCore/Boundings.h"

#include <entt/entt.hpp>

//-------------------------------------------------------------------------
// TagComponent(core) : Determines whether to expose in UI panel
//-------------------------------------------------------------------------
struct TagComponent
{
	std::string tag;
};

//-------------------------------------------------------------------------
// TransformComponent(core) : Determines whether to expose in the viewport
//-------------------------------------------------------------------------
struct TransformComponent
{
	Transform transform;
	struct Hierarchy
	{
		entt::entity parent{ entt::null };
		entt::entity firstChild{ entt::null };
		entt::entity prevSibling{ entt::null };
		entt::entity nextSibling{ entt::null };
		int depth{ 0 };
	} hierarchy;

	unsigned mWorld;
	bool     bDirtyMark;
};


//-------------------------------------------------------------------------
// CameraComponent : Camera
//-------------------------------------------------------------------------
struct CameraComponent
{
	enum class eType { Orthographic, Perspective } type;

	float cNear;
	float cFar;
	float fov;

	bool bDirtyMark;
};
inline std::string_view GetCameraTypeString(CameraComponent::eType type)
{
	switch(type)
	{
	case CameraComponent::eType::Orthographic:
		return "Orthographic";
	case CameraComponent::eType::Perspective:
		return "Perspective";
	}

	return "";
};

//-------------------------------------------------------------------------
// StaticMeshComponent : Determines whether to be rendered statically
//-------------------------------------------------------------------------
struct Geometry
{
	std::string path;

	VertexHandle vertex;
	IndexHandle  index;

	BoundingBox aabb;
};

struct Material
{
	float3 tint{ 1, 1, 1 };

	struct Data
	{
		std::string   path;
		TextureHandle handle;
	};

	Data albedo;
	Data normal;
	Data specular;
	Data ao;
	Data roughness;
	Data metallic;
	Data emission;
};

struct StaticMeshComponent
{
	Geometry geometry;
	Material material;
};

//-------------------------------------------------------------------------
// DynamicMeshComponent : Determines whether to be rendered dynamically
//-------------------------------------------------------------------------
struct DynamicMeshComponent
{
	std::string texture;
	std::string geometry;
};