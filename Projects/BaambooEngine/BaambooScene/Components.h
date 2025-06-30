#pragma once
#include "Transform.hpp"
#include "Boundings.h"

#include <entt/entt.hpp>

#include "AnimationTypes.h"


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

	Vertex* pVertices   = nullptr;
	u32     numVertices = 0;
	Index*  pIndices    = nullptr;
	u32     numIndices  = 0;

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
	std::string name;

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

	bool bDirtyMark;
};


//-------------------------------------------------------------------------
// AnimationComponent : Controls animation playback
//-------------------------------------------------------------------------
struct AnimationComponent
{
	u32   skeletonID;
	u32   currentClipID;
	float currentTime;
	float playbackSpeed;
	bool  bPlaying;
	bool  bLoop;

	// Animation blending
	struct BlendLayer
	{
		u32   clipID = INVALID_INDEX;
		float weight = 1.0f;
		float time   = 0.0f;
	};
	std::vector< BlendLayer > blendLayers;

	// Current pose
	AnimationPose currentPose;

	// Transition state
	bool  bTransitioning;
	float transitionDuration;
	float transitionTime;
	u32   transitionToClipID;
};

//-------------------------------------------------------------------------
// SkinnedMeshComponent : Links mesh to skeleton for skinning
//-------------------------------------------------------------------------
struct SkinnedMeshComponent
{
	u32 meshID          = INVALID_INDEX;
	u32 skeletonID      = INVALID_INDEX;
	u32 boneTransformID = INVALID_INDEX;
};

//-------------------------------------------------------------------------
// SkeletonComponent : Stores skeleton reference
//-------------------------------------------------------------------------
struct SkeletonComponent
{
	std::string skeletonName;
	u32 skeletonID = INVALID_INDEX;

	bool  showBones = false;
	float boneScale = 0.1f;
};


//-------------------------------------------------------------------------
// LightComponent : Determines shading
//-------------------------------------------------------------------------
enum class eLightType
{
	Directional,
	Point,
	Spot
};

struct LightComponent
{
	eLightType type = eLightType::Directional;

	float3 color = float3(1.0f, 1.0f, 1.0f);
	float  temperature_K = 0.0f;
	union
	{
		float illuminance_lux;  // directional: lux (lm/m©÷)
		float luminousPower_lm; // point/spot: lumens
	};

	float radius_m = 0.01f;
	float angularRadius_rad = 0.00465f;

	// spot light
	float innerConeAngle_rad = PI_DIV(4.0f);
	float outerConeAngle_rad = PI_DIV(3.0f);

	bool bDirtyMark;

	void SetDefaultDirectionalLight()
	{
		type = eLightType::Directional;

		temperature_K = 5778.0f;
		color = float3(1.0f, 1.0f, 1.0f);

		illuminance_lux = 3.0f;
		angularRadius_rad = 0.00465f;
	}

	void SetDefaultPoint()
	{
		type = eLightType::Point;

		temperature_K = 4000.0f;
		color = float3(1.0f, 1.0f, 1.0f);

		luminousPower_lm = 1000.0f;
		radius_m = 0.03f;
	}

	void SetDefaultSpot()
	{
		type = eLightType::Spot;

		temperature_K = 5000.0f;
		color = float3(1.0f, 1.0f, 1.0f);

		luminousPower_lm = 100.0f;
		radius_m = 0.02f;

		innerConeAngle_rad = PI_DIV(4.0f);
		outerConeAngle_rad = PI_DIV(3.0f);
	}
};
inline std::string_view GetLightTypeString(eLightType type)
{
	switch (type)
	{
	case eLightType::Directional: return "Directional";
	case eLightType::Point: return "Point";
	case eLightType::Spot: return "Spot";
	default: return "Unknown";
	}
}