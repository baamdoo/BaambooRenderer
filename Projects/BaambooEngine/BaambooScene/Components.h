#pragma once
#include "Transform.hpp"
#include "Boundings.h"
#include "ComponentTypes.h"
#include "AnimationTypes.h"

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

	float4 tint;
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

	float3 color         = float3(1.0f, 1.0f, 1.0f);
	float  temperature_K = 0.0f;
	union
	{
		float illuminance_lux; // directional: lux (lm/m©÷)
		float luminousFlux_lm; // point/spot: lumens
	};

	float radius_m = 0.01f;
	float angularRadius_rad = 0.00465f;

	// spot light
	float innerConeAngle_rad = PI_DIV(4.0f);
	float outerConeAngle_rad = PI_DIV(3.0f);

	float ev100 = 0.0f;

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

		luminousFlux_lm = 1000.0f;
		radius_m = 0.03f;
	}

	void SetDefaultSpot()
	{
		type = eLightType::Spot;

		temperature_K = 5000.0f;
		color = float3(1.0f, 1.0f, 1.0f);

		luminousFlux_lm = 100.0f;
		radius_m = 0.02f;

		innerConeAngle_rad = PI_DIV(4.0f);
		outerConeAngle_rad = PI_DIV(3.0f);
	}
};


//-------------------------------------------------------------------------
// AtmosphereComponent : Render sky by simulating scattering
//-------------------------------------------------------------------------
enum class eRaymarchResolution
{
	Low,
	Middle,
	High,
};

struct AtmosphereComponent
{
	float planetRadius_km     = 6360.0f;
	float atmosphereRadius_km = 6460.0f;

	float3 rayleighScattering  = { 5.802e-3f, 13.558e-3f, 33.1e-3f };
	float  rayleighDensityH_km = 8.0f;

	float mieScattering  = 3.996e-3f;
	float mieAbsorption  = 4.4e-3f;
	float mieDensityH_km = 1.2f;
	float miePhaseG      = 0.80f;

	float3 ozoneAbsorption = { 0.650e-3f, 1.881e-3f, 0.085e-3f };
	float  ozoneCenter_km  = 25.0f;
	float  ozoneWidth_km   = 30.0f;

	eRaymarchResolution raymarchResolution = eRaymarchResolution::Middle;
};

//-------------------------------------------------------------------------
// CloudComponent : Determines cloud shapes and effect
//-------------------------------------------------------------------------
struct CloudComponent
{
	float bottomHeight_km   = 2.0f;
	float layerThickness_km = 8.0f;

	// Light
	float3 extinctionStrength = float3(0.82f, 0.86f, 1.0f);
	float  extinctionScale    = 4.0f;

	float msContribution             = 0.8f;
	float msOcclusion                = 0.6f;
	float msEccentricity             = 0.2f;
	float groundContributionStrength = 0.3f;

	float cloudType      = 0.68f;
	float coverage       = 0.975f;
	float baseNoiseScale = 0.021f;
	float baseIntensity  = 1.0f;

	float erosionNoiseScale               = 0.220f;
	float erosionIntensity                = 0.15f;
	float erosionPower                    = 0.3f;
	float wispySkewness                   = 0.85f;
	float billowySkewness                 = 0.85f;
	float precipitation                   = 0.1f;
	float erosionHeightGradientMultiplier = 1.45f;
	float erosionHeightGradientPower      = 3.0f;

	// Animation
	float3 windDirection = float3(1.0f, 0.0f, 0.0f);
	float  windSpeed_mps = 10.0f;

	// Others
	eCloudUprezRatio uprezRatio = eCloudUprezRatio::X2;

	i32 numCloudRaymarchSteps = 64;
	i32 numLightRaymarchSteps = 64;
	float temporalBlendAlpha  = 0.05f;

	std::string blueNoiseTex;
	std::string weatherMap;
	std::string curlNoiseTex;

	bool bDirtyMark = true;
};


//-------------------------------------------------------------------------
// PostProcessComponent : Determines PostProcess Effects
//-------------------------------------------------------------------------
struct PostProcessComponent
{
	u64 effectBits = 0;

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
// ScriptComponent : Determines behaviour
//-------------------------------------------------------------------------
enum eRotationAxis
{
	Pitch = 0,
	Yaw   = 1,
	Roll  = 2
};

// TODO: test-only for now
struct ScriptComponent
{
	bool   bMove        = false;
	float3 moveVelocity = float3(0.1f, 0.0f, 0.0f);

	bool   bRotate          = false;
	float3 rotationVelocity = float3(0.0f, 0.01f, 0.0f);
};