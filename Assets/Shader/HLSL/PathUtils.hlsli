#ifndef _HLSL_PATHUTILS_HEADER
#define _HLSL_PATHUTILS_HEADER

#include "BxDF.hlsli"

#define PT_MAX_DEPTH_LIMIT 64u

#define PT_RAY_EPS          1.0e-4
#define PT_SMOOTH_ROUGHNESS 1.0e-4

#define PT_BSDF_PRINCIPLED 5u

#define PT_BSDF_FLAG_DIFFUSE      0x1u
#define PT_BSDF_FLAG_GLOSSY       0x2u
#define PT_BSDF_FLAG_TRANSMISSION 0x4u

#define PT_LOBE_EPS 1.0e-4

struct SurfacePayload
{
    float3 albedo;
    uint   hitKind;
    float3 normal;
    float  dist;
    float3 position;
    float  roughness;
    float3 geometricNormal;
    float  anisotropy;
    float3 tangent;
    float  anisotropyRotation;
    float3 emission;
    float  metallic;
    float3 specularColor;
    float  ior;
    float  transmission;
    float  clearcoat;
    float  clearcoatRoughness;
    float3 sheenColor;
    float  sheenRoughness;
    float  specularStrength;
    
    uint bPrincipled;
    uint materialID;
    uint instanceID;
    uint primitiveID;
};

struct ShadowPayload
{
    uint visible;
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
    float  pdf;
    float3 f;
    uint   flags;
    float3 weight;
    uint   valid;
    uint   lobe;
    uint   isDelta;
    uint   attempted;
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
    float a2 = pdfA * pdfA;
    float b2 = pdfB * pdfB;
    float denom = a2 + b2;
    return denom > EPSILON_MIN ? a2 / denom : 0.0;
}

float CalculateMISWeight(float prevPdfBSDF, float pdfLight, uint wasDelta)
{
    if (wasDelta != 0)
    {
        return 1.0;
    }
    
    if (prevPdfBSDF <= 0.0 || pdfLight <= 0.0)
    {
        return 1.0;
    }
    
    return PowerHeuristic(prevPdfBSDF, pdfLight);
}

float3 ReadBaseColor(MaterialData mat, float2 uv)
{
    float3 albedo = float3(mat.tintR, mat.tintG, mat.tintB);
    if (mat.albedoID != INVALID_INDEX)
    {
        Texture2D albedoTex = GetResource(mat.albedoID);
        albedo *= albedoTex.SampleLevel(g_TrilinearWrapSampler, uv, 0).rgb;
    }

    return max(albedo, float3(0.0, 0.0, 0.0));
}

float ReadOpacity(MaterialData mat, float2 uv)
{
    if (mat.alphaCutoff <= 0.0 || mat.albedoID == INVALID_INDEX)
        return 1.0;

    Texture2D albedoTex = GetResource(mat.albedoID);
    return albedoTex.SampleLevel(g_TrilinearWrapSampler, uv, 0).a;
}

float ReadRoughness(MaterialData mat, float2 uv)
{
    float roughness = mat.roughness;
    if (mat.metallicRoughnessAoID != INVALID_INDEX)
    {
        Texture2D ormTex = GetResource(mat.metallicRoughnessAoID);
        roughness *= ormTex.SampleLevel(g_TrilinearWrapSampler, uv, 0).g;
    }

    return saturate(roughness);
}

float ReadMetallic(MaterialData mat, float2 uv)
{
    float metallic = mat.metallic;
    if (mat.metallicRoughnessAoID != INVALID_INDEX)
    {
        Texture2D ormTex = GetResource(mat.metallicRoughnessAoID);
        metallic *= ormTex.SampleLevel(g_TrilinearWrapSampler, uv, 0).b;
    }

    return saturate(metallic);
}

float ReadTransmission(MaterialData mat, float2 uv)
{
    float transmission = mat.transmission;
    if (mat.transmissionID != INVALID_INDEX)
    {
        Texture2D transmissionTex = GetResource(mat.transmissionID);
        transmission *= transmissionTex.SampleLevel(g_TrilinearWrapSampler, uv, 0).r;
    }

    return saturate(transmission);
}

float ReadAnisotropy(MaterialData mat, float2 uv)
{
    float anisotropy = mat.anisotropy;
    if (mat.anisotropyID != INVALID_INDEX)
    {
        Texture2D anisotropyTex = GetResource(mat.anisotropyID);
        anisotropy *= anisotropyTex.SampleLevel(g_TrilinearWrapSampler, uv, 0).r;
    }

    return saturate(anisotropy);
}

float3 ReadEmission(MaterialData mat, float2 uv)
{
    float3 emission = float3(mat.emissionColorR, mat.emissionColorG, mat.emissionColorB) * mat.emissivePower;
    if (mat.emissiveID != INVALID_INDEX)
    {
        Texture2D emissiveTex = GetResource(mat.emissiveID);
        emission *= emissiveTex.SampleLevel(g_TrilinearWrapSampler, uv, 0).rgb;
    }

    return max(emission, float3(0.0, 0.0, 0.0));
}

float3 ReadShadingNormal(MaterialData mat, float2 uv, float3 shadingNWorld, float3 tangentOS)
{
    if (mat.normalID == INVALID_INDEX)
        return shadingNWorld;

    Texture2D normalTex = GetResource(mat.normalID);
    float3 tangentNormal = normalTex.SampleLevel(g_TrilinearWrapSampler, uv, 0).rgb * 2.0 - 1.0;
    tangentNormal = normalize(tangentNormal);

    float3 tangentWorld = mul((float3x3)ObjectToWorld3x4(), tangentOS);
    tangentWorld -= shadingNWorld * dot(tangentWorld, shadingNWorld);
    float tangentLen2 = dot(tangentWorld, tangentWorld);
    if (tangentLen2 <= EPSILON_MIN)
        return shadingNWorld;

    float3 T = tangentWorld * rsqrt(tangentLen2);
    float3 B = cross(shadingNWorld, T);
    float3 mappedNWorld = normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * shadingNWorld);
    return dot(mappedNWorld, shadingNWorld) < 0.0 ? -mappedNWorld : mappedNWorld;
}
float3 OffsetRay(float3 p, float3 n, float3 w)
{
    return p + n * ((dot(n, w) >= 0.0) ? PT_RAY_EPS : -PT_RAY_EPS);
}

// Shadow ray visibility: returns true when no occluder lies between p and target
bool IsVisible(float3 p, float3 n, float3 target)
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
    sp.visible = 0u;
    TraceRay(
        g_Scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF,
        1,
        0,
        1,
        ray,
        sp);

    return sp.visible != 0u;
}

bool IsDirectionVisible(float3 p, float3 n, float3 wi)
{
    wi = normalize(wi);
    float3 origin = OffsetRay(p, n, wi);

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = wi;
    ray.TMin      = PT_RAY_EPS;
    ray.TMax      = 1.0e30;

    ShadowPayload sp;
    sp.visible = 0u;
    TraceRay(
        g_Scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF,
        1,
        0,
        1,
        ray,
        sp);

    return sp.visible != 0u;
}


#endif // _HLSL_PATHUTILS_HEADER


