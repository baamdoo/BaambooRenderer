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

	BoundingBox    aabb;
	BoundingSphere sphere;

	Vertex* pVertices   = nullptr;
	u32     numVertices = 0;
	Index*  pIndices    = nullptr;
	u32     numIndices  = 0;

	// Meshlets
	Meshlet* pMeshlets           = nullptr;
	u32      numMeshlets         = 0;
	u32*     pMeshletVertices    = nullptr;
	u32      numMeshletVertices  = 0;
	u32*     pMeshletTriangles   = nullptr;
	u32      numMeshletTriangles = 0;
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

	float4 tint = float4(1.0f, 1.0f, 1.0f, 1.0f);

	float shininess = 32.0f;
	float metallic  = 0.0f;
	float roughness = 0.5f;
	float ior       = 1.0f;

	float alphaCutoff        = 0.0f;
	float clearcoat          = 0.0f;
	float clearcoatRoughness = 0.0f;

	float anisotropy         = 0.0f;
	float anisotropyRotation = 0.0f;

	float3 sheenColor     = float3(0.0f);
	float  sheenRoughness = 0.0f;

	float subsurface       = 0.0f;
	float transmission     = 0.0f;
	float specularStrength = 1.0f;
	float emissivePower    = 0.0f;

	std::string albedoTex;
	std::string normalTex;
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

	float3 color        = float3(1.0f, 1.0f, 1.0f);
	float  temperatureK = 0.0f;
	union
	{
		float illuminanceLux; // directional: lux (lm/m©÷)
		float luminousFluxLm; // point/spot: lumens
	};

	float radiusM          = 0.01f;
	float angularRadiusRad = 0.00465f;

	// spot light
	float innerConeAngleRad = PI_DIV(4.0f);
	float outerConeAngleRad = PI_DIV(3.0f);

	bool bDirtyMark;

	void SetDefaultDirectionalLight()
	{
		type = eLightType::Directional;

		temperatureK = 5778.0f;
		color        = float3(1.0f, 1.0f, 1.0f);

		illuminanceLux   = 3.0f;
		angularRadiusRad = 0.00465f;
	}

	void SetDefaultPoint()
	{
		type = eLightType::Point;

		temperatureK = 4000.0f;
		color        = float3(1.0f, 1.0f, 1.0f);

		luminousFluxLm = 1000.0f;
		radiusM        = 0.03f;
	}

	void SetDefaultSpot()
	{
		type = eLightType::Spot;

		temperatureK = 5000.0f;
		color        = float3(1.0f, 1.0f, 1.0f);

		luminousFluxLm = 100.0f;
		radiusM        = 0.02f;

		innerConeAngleRad = PI_DIV(4.0f);
		outerConeAngleRad = PI_DIV(3.0f);
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
	float planetRadiusKm     = 6360.0f;
	float atmosphereRadiusKm = 6460.0f;

	float3 rayleighScattering = { 5.802e-3f, 13.558e-3f, 33.1e-3f };
	float  rayleighDensityKm  = 8.0f;

	float mieScattering = 3.996e-3f;
	float mieAbsorption = 4.4e-3f;
	float mieDensityKm  = 1.2f;
	float miePhaseG     = 0.80f;

	float3 ozoneAbsorption = { 0.650e-3f, 1.881e-3f, 0.085e-3f };
	float  ozoneCenterKm   = 25.0f;
	float  ozoneWidthKm    = 30.0f;

	eRaymarchResolution raymarchResolution = eRaymarchResolution::Middle;

	std::string skybox;

	static AtmosphereComponent DayPreset()
	{
		AtmosphereComponent preset{};

		return preset;
	}
};

//-------------------------------------------------------------------------
// CloudComponent : Determines cloud shapes and effect
//-------------------------------------------------------------------------
struct CloudComponent
{
	float bottomHeightKm   = 0.6f;
	float layerThicknessKm = 0.7f;

	float shadowTracingDistanceMultiplier = 0.5f;

	float3 albedo      = float3(1.0f);
	float  albedoScale = 0.3f;

	float groundContributionStrength = 0.3f;

	// shape
	float floorVariationClear  = 0.1f;
	float floorVariationCloudy = 0.8f;

	float cloudsScale        = 1.0f;
	float cloudsMacroUvScale = 12000.0f;
	float cloudsCoverage     = 1.28f;
	float clumpsVariation    = 0.23f;

	float baseDensity         = 1.5f;
	float baseErosionScale    = 0.5f;
	float baseErosionPower    = 3.0f;
	float baseErosionStrength = 1.2f;

	float hfErosionStrength   = 0.9f;
	float hfErosionDistortion = 1.0f;

	// shade-direct
	float3 scatteringScale = float3(2.695f, 2.963334f, 3.5f);
	float  extinctionScale = 10.0f;

	float msContribution             = 0.85f;
	float msOcclusion                = 0.5f;
	float msEccentricity             = 0.4f;

	float silverScatterG = 0.99f;

	// shade-ambient
	float ambientIntensity   = 1.0f;
	float ambientSaturation  = 0.45f;
	float topAmbientScale    = 1.0f;
	float bottomAmbientScale = 0.85f;

	// Animation
	float3 windDirection = float3(1.0f, 0.0f, 0.0f);
	float  windSpeedMps = 10.0f;

	// Others
	eCloudUprezRatio uprezRatio = eCloudUprezRatio::X2;

	i32 numCloudRaymarchSteps = 128;
	i32 numLightRaymarchSteps = 16;
	float frontDepthBias      = 0.05f;
	float temporalBlendAlpha  = 0.1f;

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

		float ev100;
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