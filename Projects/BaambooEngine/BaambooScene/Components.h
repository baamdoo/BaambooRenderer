#pragma once
#include "Transform.hpp"
#include "Boundings.h"

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

	unsigned world;
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

	bool bMain;
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
struct StaticMeshComponent
{
	std::string path;

	std::vector< Vertex > vertices;
	std::vector< Index >  indices;

	bool bDirtyMark;
};

//-------------------------------------------------------------------------
// DynamicMeshComponent : Determines whether to be rendered dynamically
//-------------------------------------------------------------------------
struct DynamicMeshComponent
{
	std::string texture;
	std::string geometry;
};


//-------------------------------------------------------------------------
// MaterialComponent : Determines surface of mesh (mesh-dependent)
//-------------------------------------------------------------------------
struct MaterialComponent
{
	float3 tint{ 1, 1, 1 };
	float  roughness;
	float  metallic;

	std::string albedoTex;
	std::string normalTex;
	std::string specularTex;
	std::string aoTex;
	std::string roughnessTex;
	std::string metallicTex;
	std::string emissionTex;

	bool bDirtyMark;
};