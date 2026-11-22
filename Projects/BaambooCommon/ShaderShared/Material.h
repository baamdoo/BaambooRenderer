#pragma once
#include "Types.h"

struct MaterialTextureData
{
    u32 textureID;
    u32 semantic;
    u32 channel;
    u32 padding0;
};

#ifdef __cplusplus
#define BB_UNSCOPED_ENUM
#else
#define BB_UNSCOPED_ENUM [UnscopedEnum]
#endif

BB_UNSCOPED_ENUM
enum eMaterialTextureSemantic : u32
{
    eMaterialTextureSemantic_BaseColor = 0,
    eMaterialTextureSemantic_Opacity,
    eMaterialTextureSemantic_Normal,
    eMaterialTextureSemantic_Occlusion,
    eMaterialTextureSemantic_PerceptualRoughness,
    eMaterialTextureSemantic_Metallic,
    eMaterialTextureSemantic_Emission,
    eMaterialTextureSemantic_Specular,
    eMaterialTextureSemantic_Clearcoat,
    eMaterialTextureSemantic_Sheen,
    eMaterialTextureSemantic_Anisotropy,
    eMaterialTextureSemantic_Subsurface,
    eMaterialTextureSemantic_Transmission,
};

BB_UNSCOPED_ENUM
enum eMaterialTextureChannel : u32
{
    eMaterialTextureChannel_R = 0,
    eMaterialTextureChannel_G,
    eMaterialTextureChannel_B,
    eMaterialTextureChannel_A,
    eMaterialTextureChannel_RGB,
    eMaterialTextureChannel_RGBA,
};

BB_UNSCOPED_ENUM
enum eMaterialTextureColorSpace : u32
{
    eMaterialTextureColorSpace_Linear = 0,
    eMaterialTextureColorSpace_SRGB,
};

struct MaterialData
{
    float3 tint;
    float  metallic;

    float roughness;
    float ior;
    float alphaCutoff;
    float opacity;

    float clearcoat;
    float clearcoatRoughness;
    float anisotropy;
    float anisotropyRotation;

    float3 emissionColor;
    float  emissivePower;

    float3 specularColor;
    float  specularStrength;

    float3 sheenColor;
    float  sheenRoughness;

    float subsurface;
    float transmission;
    u32   materialType;
    u32   materialFlags;

    u32 textureOffset;
    u32 textureCount;
    u32 layerOffset;
    u32 layerCount;
};

struct MaterialSlabData
{
    u32   materialID;
    float thickness;
    float phaseG;
    u32   padding0;

    float3 sigmaA;
    float  padding1;

    float3 sigmaS;
    float  padding2;
};

static const u32 MATERIAL_FLAG_FACE_NORMALS           = 1u << 0u;
static const u32 MATERIAL_FLAG_ALPHA_MASK             = 1u << 1u;
static const u32 MATERIAL_FLAG_ALPHA_BLEND            = 1u << 2u;
static const u32 MATERIAL_FLAG_DOUBLE_SIDED           = 1u << 3u;
static const u32 MATERIAL_FLAG_THIN_WALLED            = 1u << 4u;
static const u32 MATERIAL_FLAG_RELATIVE_IOR_INTERFACE = 1u << 5u;
static const u32 MATERIAL_FLAG_MEDIUM_BOUNDARY        = 1u << 6u;

#ifdef __cplusplus
static_assert(sizeof(MaterialTextureData) == 16);
static_assert(sizeof(MaterialData) == 128);
static_assert(sizeof(MaterialSlabData) == 48);
#endif
