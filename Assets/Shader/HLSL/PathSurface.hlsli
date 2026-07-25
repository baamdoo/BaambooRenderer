#ifndef _HLSL_PATHSURFACE_HEADER
#define _HLSL_PATHSURFACE_HEADER

#include "PathUtils.hlsli"

struct SurfaceMaterial
{
    float3 albedo;
    float  roughness;
    float3 emission;
    float  metallic;
    float3 specularColor;
    float  anisotropy;
    float  anisotropyRotation;
    float  ior;
    float  transmission;
    float  clearcoat;
    float  clearcoatRoughness;
    float3 sheenColor;
    float  sheenRoughness;
    float  specularStrength;
    uint   isPrincipled;
    uint   isSmooth;
    uint   layerOffset;
    uint   layerCount;
};

bool IsPrincipledMaterial(SurfaceMaterial material)
{
    return material.isPrincipled != 0u;
}

bool HasTransmissionLobe(SurfaceMaterial material)
{
    return saturate(material.transmission) > PT_LOBE_EPS;
}

bool HasCoatingLayers(SurfaceMaterial material)
{
    return material.layerCount > 1u;
}

bool HasClearcoatLobe(SurfaceMaterial material)
{
    return saturate(material.clearcoat) > PT_LOBE_EPS;
}

bool HasSheenLobe(SurfaceMaterial material)
{
    return any(material.sheenColor > PT_LOBE_EPS);
}

float SheenSamplingWeight(SurfaceMaterial material)
{
    return max(max(material.sheenColor.x, material.sheenColor.y), material.sheenColor.z);
}

// Opaque non-metal materials may still have a dielectric specular interface (plastic, ceramic, lacquered wood).
bool HasDielectricSpecularLobe(SurfaceMaterial material, float eta)
{
    eta = max(eta, 1.0e-4);

    float etaContrast = abs(eta - 1.0) / max(eta + 1.0, 1.0e-4);
    float specularColorMax = max(material.specularColor.x, max(material.specularColor.y, material.specularColor.z));

    return !IsPrincipledMaterial(material) &&
           !HasTransmissionLobe(material) &&
           saturate(material.metallic) < 1.0 - PT_LOBE_EPS &&
           saturate(material.specularStrength) > PT_LOBE_EPS &&
           specularColorMax > PT_LOBE_EPS &&
           etaContrast > PT_LOBE_EPS;
}

SurfaceMaterial LoadSurfaceMaterial(uint materialID, float2 uv)
{
    StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);
    const bool bHasMaterial = materialID != INVALID_INDEX;
    MaterialData mat = (MaterialData)0;
    if (bHasMaterial)
        mat = Materials[materialID];

    SurfaceMaterial material;
    material.albedo             = bHasMaterial ? ReadBaseColor(mat, uv) : float3(1.0, 0.0, 1.0);
    material.roughness          = bHasMaterial ? ReadRoughness(mat, uv) : 1.0;
    material.emission           = bHasMaterial ? ReadEmission(mat, uv) : float3(0.0, 0.0, 0.0);
    material.metallic           = bHasMaterial ? ReadMetallic(mat, uv) : 0.0;
    material.specularColor      = bHasMaterial ? float3(mat.specularColorR, mat.specularColorG, mat.specularColorB) : float3(0.04, 0.04, 0.04);
    material.anisotropy         = bHasMaterial ? ReadAnisotropy(mat, uv) : 0.0;
    material.anisotropyRotation = bHasMaterial ? mat.anisotropyRotation : 0.0;
    material.ior                = bHasMaterial ? max(mat.ior, 1.0e-4) : 1.0;
    material.transmission       = bHasMaterial ? ReadTransmission(mat, uv) : 0.0;
    material.clearcoat          = bHasMaterial ? ReadClearcoat(mat, uv) : 0.0;
    material.clearcoatRoughness = bHasMaterial ? mat.clearcoatRoughness : 0.0;
    material.sheenColor         = bHasMaterial ? float3(mat.sheenColorR, mat.sheenColorG, mat.sheenColorB) : float3(0.0, 0.0, 0.0);
    material.sheenRoughness     = bHasMaterial ? mat.sheenRoughness : 0.0;
    material.specularStrength   = bHasMaterial ? mat.specularStrength : 1.0;
    material.isPrincipled       = (bHasMaterial && mat.materialType == PT_BSDF_PRINCIPLED) ? 1u : 0u;

    const float alphaRaw = material.isPrincipled != 0u
        ? material.roughness * material.roughness
        : material.roughness;
    material.isSmooth    = alphaRaw <= PT_SMOOTH_ALPHA_THRESHOLD ? 1u : 0u;
    material.layerOffset = bHasMaterial ? mat.layerOffset : INVALID_INDEX;
    material.layerCount  = bHasMaterial ? max(mat.layerCount, 1u) : 1u;
    return material;
}

void BuildSurfaceONB(float3 n, out float3 t, out float3 b)
{
    const float sign = (n.z >= 0.0) ? 1.0 : -1.0;
    const float a = -1.0 / (sign + n.z);
    const float h = n.x * n.y * a;

    t = float3(1.0 + sign * n.x * n.x * a, sign * h, -sign * n.x);
    b = float3(h, sign + n.y * n.y * a, -n.y);
}

// Tangent frame at the hit point
BxDF::Frame MakeSurfaceFrame(SurfacePayload hp)
{
    BxDF::Frame frame;
    frame.N = hp.normal;

    float3 T = hp.tangent - frame.N * dot(hp.tangent, frame.N);
    float tangentLen2 = dot(T, T);
    if (tangentLen2 <= EPSILON_MIN)
    {
        BuildSurfaceONB(frame.N, frame.T, frame.B);
    }
    else
    {
        frame.T = T * rsqrt(tangentLen2);
        frame.B = cross(frame.N, frame.T);
    }

    return frame;
}

float GetAlpha(SurfaceMaterial material)
{
    if (material.isSmooth)
        return 0.0;

    float alphaRaw = IsPrincipledMaterial(material) ? material.roughness * material.roughness : material.roughness;
    return max(alphaRaw, PT_MIN_ROUGHNESS_ALPHA);
}

// Stretch alpha into tangent/bitangent roughness for anisotropic highlights
float2 GetAlpha2(SurfaceMaterial material)
{
    if (material.isSmooth)
        return 0.0;

    float alpha      = GetAlpha(material);
    float anisotropy = saturate(material.anisotropy);
    if (anisotropy <= PT_LOBE_EPS)
        return float2(alpha, alpha);

    float aspect = sqrt(max(1.0e-4, 1.0 - 0.9 * anisotropy));
    return float2(max(alpha / aspect, 1.0e-3), max(alpha * aspect, 1.0e-3));
}

#endif // _HLSL_PATHSURFACE_HEADER
