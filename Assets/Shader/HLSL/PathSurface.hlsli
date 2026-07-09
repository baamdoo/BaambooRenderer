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
    uint   bPrincipled;
};

bool IsPrincipledMaterial(SurfaceMaterial material)
{
    return material.bPrincipled != 0u;
}

bool HasTransmissionLobe(SurfaceMaterial material)
{
    return saturate(material.transmission) > PT_LOBE_EPS;
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
bool HasDielectricSpecularLobe(SurfaceMaterial material)
{
    return !IsPrincipledMaterial(material) &&
           !HasTransmissionLobe(material) &&
           saturate(material.metallic) < 1.0 - PT_LOBE_EPS &&
           saturate(material.specularStrength) > PT_LOBE_EPS &&
           max(material.ior, 1.0) > 1.0 + PT_LOBE_EPS;
}

SurfaceMaterial MakeSurfaceMaterial(SurfacePayload hp)
{
    SurfaceMaterial material;
    material.albedo             = hp.albedo;
    material.roughness          = hp.roughness;
    material.emission           = hp.emission;
    material.metallic           = hp.metallic;
    material.specularColor      = hp.specularColor;
    material.anisotropy         = hp.anisotropy;
    material.anisotropyRotation = hp.anisotropyRotation;
    material.ior                = hp.ior;
    material.transmission       = hp.transmission;
    material.clearcoat          = hp.clearcoat;
    material.clearcoatRoughness = hp.clearcoatRoughness;
    material.sheenColor         = hp.sheenColor;
    material.sheenRoughness     = hp.sheenRoughness;
    material.specularStrength   = hp.specularStrength;
    material.bPrincipled        = hp.bPrincipled;
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

    float rotation = hp.anisotropyRotation;
    if (abs(rotation) > PT_LOBE_EPS)
    {
        float s = sin(rotation);
        float c = cos(rotation);
        float3 rotatedT = c * frame.T + s * frame.B;
        float3 rotatedB = -s * frame.T + c * frame.B;
        frame.T = rotatedT;
        frame.B = rotatedB;
    }

    return frame;
}

float GetAlpha(SurfaceMaterial material)
{
    float roughness = max(material.roughness, 1.0e-3);
    return IsPrincipledMaterial(material) ? max(roughness * roughness, 1.0e-3) : roughness;
}

// Stretch alpha into tangent/bitangent roughness for anisotropic highlights
float2 GetAlpha2(SurfaceMaterial material)
{
    float alpha = GetAlpha(material);
    float anisotropy = saturate(material.anisotropy);
    if (anisotropy <= PT_LOBE_EPS)
        return float2(alpha, alpha);

    float aspect = sqrt(max(1.0e-4, 1.0 - 0.9 * anisotropy));
    return float2(max(alpha / aspect, 1.0e-3), max(alpha * aspect, 1.0e-3));
}

#endif // _HLSL_PATHSURFACE_HEADER