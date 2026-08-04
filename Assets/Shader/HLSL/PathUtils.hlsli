#ifndef _HLSL_PATHUTILS_HEADER
#define _HLSL_PATHUTILS_HEADER

#include "BxDF.hlsli"
#include "MaterialTextures.hlsli"

#define PT_MAX_DEPTH_LIMIT 64u

#define PT_RAY_EPS          1.0e-4

#define PT_BSDF_PRINCIPLED 5u

#define PT_BSDF_FLAG_DIFFUSE      0x1u
#define PT_BSDF_FLAG_GLOSSY       0x2u
#define PT_BSDF_FLAG_TRANSMISSION 0x4u

#define PT_LOBE_EPS 1.0e-4

#define PT_MIN_ROUGHNESS_ALPHA    1.0e-3
#define PT_SMOOTH_ALPHA_THRESHOLD 1.0e-4

struct SurfacePayload
{
    float2 barycentrics;
    float  dist;
    uint   hitKind;

    uint instanceID;
    uint primitiveID;
};

struct SurfaceData
{
    float3 normal;
    float  dist;

    float3 geometricNormal;
    uint   hitKind;

    float4 tangent; // xyz: world-space tangent, w: authored tangent-frame handedness in world space.

    float2 uv;
    uint   instanceID;
    uint   primitiveID;

    float2 ddxUV;
    float2 ddyUV;
};

struct ShadowPayload
{
    uint visible;
    uint alphaSeed;
};


struct PathContribution
{
    float3 diffuse;
    float3 specular;
    float3 transmission;
};

PathContribution ZeroPathContribution()
{
    PathContribution contribution;
    contribution.diffuse      = float3(0.0, 0.0, 0.0);
    contribution.specular     = float3(0.0, 0.0, 0.0);
    contribution.transmission = float3(0.0, 0.0, 0.0);
    return contribution;
}

struct PathBSDFSample
{
    float3 wi;
    uint   flags;
    float3 weight;      // Sampled-history continuation estimator C_H / q_H.
    uint   valid;
    uint   lobe;
    uint   isDelta;
    uint   attempted;
    float  rrEtaScale;
};

struct RayCone
{
    // Signed half-width. Refraction can place the cone focus ahead of the
    // current origin, producing a negative radius until that focus is crossed.
    // Use abs(radius) only when resolving a texture footprint.
    float radius;

    // Tangent of half the signed full-spread angle. Distance propagation keeps
    // the orientation through: radius += distance * tanHalfAngle.
    float tanHalfAngle;
};


bool IsPathFinite(float v)
{
    return (v == v) && abs(v) < 3.402823e+38;
}

bool IsPathFinite3(float3 v)
{
    return all(v == v) && all(abs(v) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38));
}

// MIS weight
float PowerHeuristic(float pdfA, float pdfB)
{
    pdfA = (IsPathFinite(pdfA) && pdfA > 0.0) ? pdfA : 0.0;
    pdfB = (IsPathFinite(pdfB) && pdfB > 0.0) ? pdfB : 0.0;
    float maxPDF = max(pdfA, pdfB);
    if (maxPDF <= 0.0)
        return 0.0;

    float a = pdfA / maxPDF;
    float b = pdfB / maxPDF;
    float a2 = a * a;
    float b2 = b * b;
    return a2 / (a2 + b2);
}

float CalculateMISWeight(float prevMarginalPDF, float pdfLight, uint wasDelta)
{
    if (wasDelta != 0)
        return 1.0;

    if (!IsPathFinite(pdfLight) || pdfLight <= 0.0)
        return 1.0;

    if (!IsPathFinite(prevMarginalPDF) || prevMarginalPDF <= 0.0)
        return 0.0;

    return PowerHeuristic(prevMarginalPDF, pdfLight);
}

float3 ReadBaseColor(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float3 albedo = float3(mat.tintR, mat.tintG, mat.tintB);

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_BASE_COLOR,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        albedo *= textureValue.rgb;
    }

    return max(albedo, float3(0.0, 0.0, 0.0));
}

float ReadOpacityLevel(MaterialData mat, float2 uv)
{
    float opacity = mat.opacity;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureLevel(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_OPACITY,
            uv,
            textureValue,
            channel))
    {
        opacity *= SelectMaterialTextureScalar(textureValue, channel);
    }
    return saturate(opacity);
}


float ReadOpacity(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float opacity = mat.opacity;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_OPACITY,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        opacity *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(opacity);
}

float ReadRoughness(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float roughness = mat.roughness;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_PERCEPTUAL_ROUGHNESS,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        roughness *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(roughness);
}

float ReadMetallic(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float metallic = mat.metallic;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_METALLIC,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        metallic *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(metallic);
}

float ReadOcclusion(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float occlusion = 1.0;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_OCCLUSION,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        occlusion *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(occlusion);
}

float ReadTransmission(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float transmission = mat.transmission;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_TRANSMISSION,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        transmission *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(transmission);
}

float ReadClearcoat(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float clearcoat = mat.clearcoat;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_CLEARCOAT,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        clearcoat *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(clearcoat);
}

void ReadAnisotropy(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV, out float anisotropy, out float anisotropyRotation)
{
    anisotropy = saturate(mat.anisotropy);

    float rotationSin, rotationCos;
    sincos(mat.anisotropyRotation, rotationSin, rotationCos);
    float2 direction = float2(rotationCos, rotationSin);

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_ANISOTROPY,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        float2 textureDirection = textureValue.rg * 2.0 - 1.0;
        float directionLen2 = dot(textureDirection, textureDirection);
        textureDirection = directionLen2 > EPSILON_MIN
            ? textureDirection * rsqrt(directionLen2)
            : float2(1.0, 0.0);

        direction = float2(
            textureDirection.x * rotationCos - textureDirection.y * rotationSin,
            textureDirection.x * rotationSin + textureDirection.y * rotationCos);
        anisotropy *= textureValue.b;
    }

    anisotropy = saturate(anisotropy);
    anisotropyRotation = atan2(direction.y, direction.x);
}

float3 ReadEmission(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float3 emission =
        float3(mat.emissionColorR, mat.emissionColorG, mat.emissionColorB) *
        mat.emissivePower;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_EMISSION,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        emission *= textureValue.rgb;
    }

    return max(emission, float3(0.0, 0.0, 0.0));
}

float3 ReadShadingNormal(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV, float3 shadingNormalWS, float3 tangentWS, inout float tangentSignWS)
{
    float4 textureValue;
    uint channel;
    if (!SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_NORMAL,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        return shadingNormalWS;
    }

    float3 normalTS = normalize(textureValue.rgb * 2.0 - 1.0);

    float3 projNfromT = shadingNormalWS * dot(tangentWS, shadingNormalWS);
    tangentWS -= projNfromT; // gram-schmidt

    float tangentWSLen2 = dot(tangentWS, tangentWS);
    if (tangentWSLen2 <= EPSILON_MIN)
        return shadingNormalWS;

    float3 T = tangentWS * rsqrt(tangentWSLen2);
    float3 B = tangentSignWS * cross(shadingNormalWS, T);
    float3 normalWS = normalize(
        normalTS.x * T +
        normalTS.y * B +
        normalTS.z * shadingNormalWS);
    if (dot(normalWS, shadingNormalWS) < 0.0)
    {
        normalWS = -normalWS;
        tangentSignWS = -tangentSignWS;
    }

    return normalWS;
}
float3 OffsetRay(float3 p, float3 n, float3 w)
{
    return p + n * ((dot(n, w) >= 0.0) ? PT_RAY_EPS : -PT_RAY_EPS);
}

// Shadow ray visibility: returns true when no occluder lies between p and target
bool IsVisible(float3 p, float3 n, float3 target, uint alphaSeed)
{
    float3 toTarget = target - p;
    float3 wi       = normalize(toTarget);
    float3 origin   = OffsetRay(p, n, wi);

    float3 offsetToTarget = target - origin;
    float  tMax           = length(offsetToTarget);
    if (tMax <= PT_RAY_EPS)
        return false;

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = offsetToTarget / tMax;
    ray.TMin      = PT_RAY_EPS;
    ray.TMax      = max(PT_RAY_EPS, tMax - PT_RAY_EPS);

    ShadowPayload sp;
    sp.visible   = 0u;
    sp.alphaSeed = alphaSeed;
    TraceRay(
        g_Scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_NON_OPAQUE,
        0xFF,
        1,
        0,
        1,
        ray,
        sp);

    return sp.visible != 0u;
}

bool IsDirectionVisible(float3 p, float3 n, float3 wi, uint alphaSeed)
{
    wi = normalize(wi);
    float3 origin = OffsetRay(p, n, wi);

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = wi;
    ray.TMin      = PT_RAY_EPS;
    ray.TMax      = 1.0e30;

    ShadowPayload sp;
    sp.visible   = 0u;
    sp.alphaSeed = alphaSeed;
    TraceRay(
        g_Scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_NON_OPAQUE,
        0xFF,
        1,
        0,
        1,
        ray,
        sp);

    return sp.visible != 0u;
}


#endif // _HLSL_PATHUTILS_HEADER


