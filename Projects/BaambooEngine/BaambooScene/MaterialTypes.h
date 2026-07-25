#pragma once

#include "Primitives.h"
#include "MathTypes.h"

#include <string>

namespace baamboo
{

// Complete local surface closure used by one boundary of a material stack.
// Texture strings remain authoring paths until MeshSystem resolves them for the GPU.
struct MaterialData
{
	std::string name;

	float4 tint = float4(1.0f);

	float metallic  = 0.0f;
	float roughness = 0.5f;
	float ior       = 1.0f;

	float alphaCutoff        = 0.0f;
	float clearcoat          = 0.0f;
	float clearcoatRoughness = 0.0f;

	float anisotropy         = 0.0f;
	float anisotropyRotation = 0.0f;

	float3 specularColor    = float3(1.0f);
	float  specularStrength = 1.0f;

	float3 sheenColor     = float3(0.0f);
	float  sheenRoughness = 0.0f;

	float3 emissionColor = float3(1.0f);
	float  emissivePower  = 0.0f;

	float subsurface   = 0.0f;
	float transmission = 0.0f;
	u32   materialType = 0u;

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

// One top-to-bottom stack entry: a surface boundary and the slab directly below it.
struct MaterialLayer
{
	MaterialData material;

	float3 sigmaA    = float3(0.0f);
	float  thickness = 0.0f;

	float3 sigmaS = float3(0.0f);
	float  phaseG = 0.0f;
};

} // namespace baamboo
