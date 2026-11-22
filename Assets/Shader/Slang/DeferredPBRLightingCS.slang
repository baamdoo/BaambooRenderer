#include "ShaderShared/Scene.h"
#include "ShaderShared/Light.h"
#include "ShaderShared/Material.h"
#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/Math.slangh"
#include "Common/Color.slangh"
#include "Common/Atmosphere.slangh"
#include "Common/LightCulling.slangh"
#include "Common/Lights.slangh"
#include "Common/MaterialTextures.slangh"
#include "Common/SurfaceResolve.slangh"
#include "Common/VisibilityBuffer.slangh"

struct DeferredLightingPush
{
    DeferredLightingConstants constants;

    uint ID_Scene;
    uint ID_DepthBuffer;
    uint ID_VBuf0;
    uint ID_VBuf1;
    uint ID_CoreNormal;
    uint ID_CoreMaterial;
    uint ID_AerialPerspectiveLUT;
    uint ID_CloudScatteringLUT;
    uint ID_SkyboxLUT;
    uint ID_LtcMatrixLUT;
    uint ID_LtcAmplitudeLUT;
    uint ID_LightGridBuffer;
    uint ID_LightListDataBuffer;
    uint ID_VoxelChunkDescs;
    uint ID_OutSceneTexture;
};

[[vk::push_constant]] ConstantBuffer< DeferredLightingPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

static const float MIN_ROUGHNESS = 0.045;

// =============================================================================
// LTC LUT layout (Heitz 2016, selfshadow baked DDS)
// =============================================================================
static const float LTC_LUT_SIZE  = 64.0;
static const float LTC_LUT_SCALE = (LTC_LUT_SIZE - 1.0) / LTC_LUT_SIZE;
static const float LTC_LUT_BIAS  = 0.5 / LTC_LUT_SIZE;

static const int DISK_NUM_SAMPLES = 16;

// =============================================================================
// BRDF Building Blocks (Cook-Torrance GGX)
// =============================================================================
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    const float a    = roughness * roughness;
    const float a2   = a * a;
    const float NoH  = max(dot(N, H), 0.0);
    const float NoH2 = NoH * NoH;

    return a2 / (PI * pow(NoH2 * (a2 - 1.0) + 1.0, 2.0));
}

float GeometrySchlickGGX(float NoV, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return NoV / (NoV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    const float NoL = max(dot(N, L), 0.0);
    const float NoV = max(dot(N, V), 0.0);
    return GeometrySchlickGGX(NoL, roughness) * GeometrySchlickGGX(NoV, roughness);
}

float3 CalculateBRDF(float3 N, float3 V, float3 L, float metallic, float roughness, float3 F0, out float3 kD)
{
    const float3 H = normalize(V + L);

    const float  D = DistributionGGX(N, H, roughness);
    const float  G = GeometrySmith(N, V, L, roughness);
    const float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    const float3 kS = F;
    kD = (float3(1.0) - kS) * (1.0 - metallic);

    const float3 numerator   = D * G * F;
    const float  denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + EPSILON_MIN;

    return numerator / denominator;
}

float3 LightColor(float3 color, float temperatureK)
{
    return temperatureK > 0.0 ? color * ColorTemperatureToRGB(temperatureK) : color;
}

// =============================================================================
// View-aligned TBN (LTC entry frame)
// =============================================================================
float3x3 BuildViewAlignedTBN(float3 N, float3 V, out float NoV)
{
    NoV = saturate(dot(N, -V));

    float3 surfaceX = -V - N * NoV;
    const float lenSq = dot(surfaceX, surfaceX);
    if (lenSq < 1e-6)
    {
        const float3 axis = (abs(N.y) < 0.9) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        surfaceX = normalize(axis - N * dot(axis, N));
    }
    else
    {
        surfaceX = surfaceX * rsqrt(lenSq);
    }
    const float3 surfaceY = cross(N, surfaceX);

    return float3x3(surfaceX, surfaceY, N);
}

// =============================================================================
// LTC polygon integral helpers (Heitz 2016, selfshadow rational fit)
// =============================================================================
float2 LtcLutCoords(float NoV, float roughness)
{
    const float u = roughness;
    const float v = sqrt(1.0 - NoV);
    return float2(u, v) * LTC_LUT_SCALE + LTC_LUT_BIAS;
}

float4 SampleLTC(Texture2D< float4 > LTC, float NoV, float roughness)
{
    return LTC.SampleLevel(g_LinearClampSampler, LtcLutCoords(NoV, roughness), 0);
}

// The matrix LUT holds (m00, m02, m11, m20) of the inverse LTC matrix; m11 is normalized to 1
float3x3 LtcWorldToFit(Texture2D< float4 > ltcMatrix, float3x3 mWorldToLTC, float NoV, float roughness)
{
    const float4   m         = SampleLTC(ltcMatrix, NoV, roughness);
    const float3x3 mLtcToFit = float3x3(
        m.x, 0.0, m.z,
        0.0, 1.0, 0.0,
        m.y, 0.0, m.w);
    return mul(mLtcToFit, mWorldToLTC);
}

float3 IntegrateEdgeVec(float3 v1, float3 v2)
{
    const float x = dot(v1, v2);
    const float y = abs(x);
    const float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    const float b = 3.4175940 + (4.1616724 + y) * y;
    const float v = a / b;
    const float theta_sintheta = (x > 0.0) ? v : 0.5 * rsqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2) * theta_sintheta;
}

float IntegrateEdge(float3 v1, float3 v2)
{
    return IntegrateEdgeVec(v1, v2).z;
}

void ClipQuadToHorizon(inout float3 L[17], out int n)
{
    int config = 0;
    if (L[0].z > 0.0) config |= 1;
    if (L[1].z > 0.0) config |= 2;
    if (L[2].z > 0.0) config |= 4;
    if (L[3].z > 0.0) config |= 8;

    n = 0;
    if (config == 0) { /* below: n = 0 */ }
    else if (config == 1) {
        n = 3;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 2) {
        n = 3;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 3) {
        n = 4;
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
        L[3] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 4) {
        n = 3;
        L[0] = -L[3].z * L[2] + L[2].z * L[3];
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
    }
    else if (config == 5) { n = 0; /* 0,2 above: impossible */ }
    else if (config == 6) {
        n = 4;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 7) {
        n = 5;
        L[4] = -L[3].z * L[0] + L[0].z * L[3];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 8) {
        n = 3;
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
        L[1] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = L[3];
    }
    else if (config == 9) {
        n = 4;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[2].z * L[3] + L[3].z * L[2];
    }
    else if (config == 10) { n = 0; /* 1,3 above: impossible */ }
    else if (config == 11) {
        n = 5;
        L[4] = L[3];
        L[3] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 12) {
        n = 4;
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
    }
    else if (config == 13) {
        n = 5;
        L[4] = L[3];
        L[3] = L[2];
        L[2] = -L[1].z * L[2] + L[2].z * L[1];
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
    }
    else if (config == 14) {
        n = 5;
        L[4] = -L[0].z * L[3] + L[3].z * L[0];
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
    }
    else { n = 4; }
}

// Sutherland-Hodgman variant: clip an N-vertex polygon (N <= 16) to the upper hemisphere (z >= 0)
void ClipPolygonToHorizon(inout float3 L[17], int N, out int n)
{
    float3 clipped[17];
    int    count = 0;

    [loop]
    for (int i = 0; i < N; ++i)
    {
        const float3 va  = L[i];
        const float3 vb  = L[(i + 1) % N];
        const bool   inA = va.z > 0.0;
        const bool   inB = vb.z > 0.0;

        if (inA && inB)
        {
            clipped[count++] = vb;
        }
        else if (inA && !inB)
        {
            clipped[count++] = lerp(va, vb, va.z / (va.z - vb.z));
        }
        else if (!inA && inB)
        {
            clipped[count++] = lerp(va, vb, va.z / (va.z - vb.z));
            clipped[count++] = vb;
        }
    }

    [loop]
    for (int j = 0; j < count; ++j)
        L[j] = clipped[j];
    n = count;
}

float PolygonRadiance(float3 P[17], int n)
{
    float sum = 0.0;
    [loop]
    for (int i = 0; i < n; ++i)
    {
        const int j = (i + 1) % n;
        sum += IntegrateEdge(P[i], P[j]);
    }

    return max(0.0, sum * (1.0 / (2.0 * PI)));
}

// =============================================================================
// Light Apply (analytic: Directional / Spot / Sphere / Tube)
// V points from the camera to the surface
// =============================================================================
float3 ApplyDirectionalLight(DirectionalLight light, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    const float3 L = normalize(-light.direction);

    float3 kD;
    const float3 specular = CalculateBRDF(N, -V, L, metallic, roughness, F0, kD);

    const float  NoL       = max(dot(N, L), 0.0);
    const float3 luminance = LightColor(light.color, light.temperatureK) * light.illuminanceLux;

    return (kD * albedo / PI + specular) * luminance * NoL;
}

float3 ApplySpotLight(SpotLight light, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    if (light.outerConeAngleRad <= 0.0)
        return float3(0.0);

    float3 L = light.position - P;
    const float distance = length(L);
    L /= distance;

    // cubic smoothstep: zero derivative at both cone edges; a squared falloff leaves a kink at the inner edge
    // that shows as hyperbolic streaks on flat surfaces nearly parallel to the cone axis
    const float cosTheta        = dot(L, normalize(-light.direction));
    const float cosThetaInner   = cos(light.innerConeAngleRad);
    const float cosThetaOuter   = cos(light.outerConeAngleRad);
    const float spotAttenuation = smoothstep(cosThetaOuter, cosThetaInner, cosTheta);

    if (spotAttenuation == 0.0)
        return float3(0.0);

    float3 kD;
    const float3 specular = CalculateBRDF(N, -V, L, metallic, roughness, F0, kD);

    const float NoL               = max(dot(N, L), 0.0);
    const float solidAngle        = 2.0 * PI * (1.0 - cosThetaOuter);
    const float luminousIntensity = light.luminousFluxLm / solidAngle;

    const float rMax        = InfluenceRadiusCone(light.luminousFluxLm, light.outerConeAngleRad, light.radiusM);
    const float window      = SmoothDistanceAtt(distance * distance, 1.0 / max(rMax * rMax, 1e-6));
    const float attenuation = 1.0 / max(distance * distance, light.radiusM * light.radiusM) * window;

    const float3 luminance = LightColor(light.color, light.temperatureK) * luminousIntensity * attenuation * spotAttenuation;
    return (kD * albedo / PI + specular) * luminance * NoL;
}

float3 ApplySphereLight(SphereLight light, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    const float3 L = light.position - P;
    const float  d = length(L);

    const float3 R = reflect(V, N);

    const float  LoR         = dot(L, R);
    const float3 centerToRay = LoR * R - L;

    const float3 closestPt         = L + centerToRay * saturate(light.radius / max(length(centerToRay), 1e-6));
    const float3 representativeRay = normalize(closestPt);

    const float originalAlpha = roughness * roughness;
    const float modifiedAlpha = saturate(originalAlpha + light.radius / (2.0 * d));
    const float jacobian      = (originalAlpha * originalAlpha) / max(modifiedAlpha * modifiedAlpha, 1e-6);

    float3 kD;
    const float3 specular = CalculateBRDF(N, -V, representativeRay, metallic, sqrt(modifiedAlpha), F0, kD) * jacobian;

    const float I = light.luminousFluxLm / (4.0 * PI);

    const float rMax        = InfluenceRadiusIsotropic(light.luminousFluxLm, light.radius);
    const float window      = SmoothDistanceAtt(d * d, 1.0 / max(rMax * rMax, 1e-6));
    const float attenuation = 1.0 / max(d * d, light.radius * light.radius) * window;

    const float NoL = max(dot(N, L / d), 0.0);

    const float3 luminance = LightColor(light.color, light.temperatureK) * I * attenuation;
    return (kD * albedo / PI + specular) * luminance * NoL;
}

float3 ApplyTubeLight(TubeLight light, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    const float3 R = reflect(V, N);

    const float3 L0 = light.positionA - P;
    const float3 L1 = light.positionB - P;
    const float3 Ld = L1 - L0;

    const float Ld2 = dot(Ld, Ld);

    const float  tSpec     = (dot(R, L0) * dot(R, Ld) - dot(L0, Ld)) / max(Ld2 - dot(R, Ld) * dot(R, Ld), 1e-6);
    const float3 closestPt = L0 + saturate(tSpec) * Ld;

    const float3 centerToRay = dot(closestPt, R) * R - closestPt;
    const float3 Lspec       = closestPt + centerToRay * saturate(light.radius / max(length(centerToRay), 1e-5));

    const float dSpec = length(Lspec);

    const float originalAlpha = roughness * roughness;
    const float modifiedAlpha = saturate(originalAlpha + light.radius / (2.0 * dSpec));
    const float jacobian      = (originalAlpha * originalAlpha) / max(modifiedAlpha * modifiedAlpha, 1e-6);

    float3 kD;
    const float3 specular = CalculateBRDF(N, -V, Lspec / max(dSpec, 1e-6), metallic, sqrt(modifiedAlpha), F0, kD) * jacobian;

    const float  tDiff = saturate(dot(-L0, Ld) / max(Ld2, 1e-6));
    const float3 Ldiff = L0 + tDiff * Ld;

    const float dDiff = length(Ldiff);
    const float NoL   = max(dot(N, Ldiff / max(dDiff, 1e-6)), 0.0);

    const float I = light.luminousFluxLm / (4.0 * PI);

    const float rMax        = InfluenceRadiusIsotropic(light.luminousFluxLm, light.radius);
    const float window      = SmoothDistanceAtt(dDiff * dDiff, 1.0 / max(rMax * rMax, 1e-6));
    const float attenuation = 1.0 / max(dDiff * dDiff, light.radius * light.radius) * window;

    const float3 luminance = LightColor(light.color, light.temperatureK) * I * attenuation;
    return (kD * albedo / PI + specular) * luminance * NoL;
}

// =============================================================================
// Light Apply (LTC: area, disk; Heitz 2016)
// Reference: https://github.com/selfshadow/ltc_code
// =============================================================================
float3 ApplyAreaLight(
    AreaLight light, float3 P, float3 N, float3 V,
    float3 albedo, float metallic, float roughness, float3 F0,
    Texture2D< float4 > ltcMatrix, Texture2D< float4 > ltcAmplitude)
{
    // single-sided: the normal is the back-face direction
    if (dot(light.normal, P - light.position) > 0.0)
        return float3(0.0);

    const float3 bitangent = cross(light.normal, light.tangent);
    const float3 corner[4] = {
        light.position - light.tangent * light.halfWidth - bitangent * light.halfHeight, // bottom-left
        light.position + light.tangent * light.halfWidth - bitangent * light.halfHeight, // bottom-right
        light.position + light.tangent * light.halfWidth + bitangent * light.halfHeight, // top-right
        light.position - light.tangent * light.halfWidth + bitangent * light.halfHeight  // top-left
    };

    float NoV;
    const float3x3 mWorldToLTC = BuildViewAlignedTBN(N, V, NoV);
    const float3x3 M           = LtcWorldToFit(ltcMatrix, mWorldToLTC, NoV, roughness);

    float3 Pspec[17];
    float3 Pdiff[17];
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        Pspec[i] = normalize(mul(M, corner[i] - P));
        Pdiff[i] = normalize(mul(mWorldToLTC, corner[i] - P));
    }

    int Nspec, Ndiff;
    ClipQuadToHorizon(Pspec, Nspec);
    ClipQuadToHorizon(Pdiff, Ndiff);

    const float Espec = PolygonRadiance(Pspec, Nspec);
    const float Ediff = PolygonRadiance(Pdiff, Ndiff);

    const float4 amplitude = SampleLTC(ltcAmplitude, NoV, roughness);
    const float3 F         = F0 * amplitude.x + (1.0 - F0) * amplitude.y;
    const float3 specular  = F * Espec;

    const float3 kD      = (1.0 - F0) * (1.0 - metallic);
    const float3 diffuse = kD * albedo * Ediff;

    const float  area      = (2.0 * light.halfWidth) * (2.0 * light.halfHeight);
    const float3 luminance = LightColor(light.color, light.temperatureK) * light.luminousFluxLm / (PI * area);

    const float rMax   = InfluenceRadiusIsotropic(light.luminousFluxLm, sqrt(light.halfWidth * light.halfWidth + light.halfHeight * light.halfHeight));
    const float distSq = dot(P - light.position, P - light.position);
    return (diffuse + specular) * luminance * SmoothDistanceAtt(distSq, 1.0 / max(rMax * rMax, 1e-6));
}

float3 ApplyDiskLight(
    DiskLight light, float3 P, float3 N, float3 V,
    float3 albedo, float metallic, float roughness, float3 F0,
    Texture2D< float4 > ltcMatrix, Texture2D< float4 > ltcAmplitude)
{
    if (dot(light.normal, P - light.position) > 0.0)
        return float3(0.0);

    // disk plane basis: tangent (azimuth zero) + bitangent
    const float3 bitangent = cross(light.normal, light.tangent);

    float NoV;
    const float3x3 mWorldToLTC = BuildViewAlignedTBN(N, V, NoV);
    const float3x3 M           = LtcWorldToFit(ltcMatrix, mWorldToLTC, NoV, roughness);

    float3 Pspec[DISK_NUM_SAMPLES + 1];
    float3 Pdiff[DISK_NUM_SAMPLES + 1];
    [unroll]
    for (int i = 0; i < DISK_NUM_SAMPLES; ++i)
    {
        const float  theta  = 2.0 * PI * float(i) / float(DISK_NUM_SAMPLES);
        const float3 corner = light.position + light.radius * (cos(theta) * light.tangent + sin(theta) * bitangent);

        Pspec[i] = normalize(mul(M, corner - P));
        Pdiff[i] = normalize(mul(mWorldToLTC, corner - P));
    }

    int Nspec, Ndiff;
    ClipPolygonToHorizon(Pspec, DISK_NUM_SAMPLES, Nspec);
    ClipPolygonToHorizon(Pdiff, DISK_NUM_SAMPLES, Ndiff);

    const float Espec = PolygonRadiance(Pspec, Nspec);
    const float Ediff = PolygonRadiance(Pdiff, Ndiff);

    const float4 amplitude = SampleLTC(ltcAmplitude, NoV, roughness);
    const float3 F         = F0 * amplitude.x + (1.0 - F0) * amplitude.y;
    const float3 specular  = F * Espec;

    const float3 kD      = (1.0 - F0) * (1.0 - metallic);
    const float3 diffuse = kD * albedo * Ediff;

    const float  area      = PI * light.radius * light.radius;
    const float3 luminance = LightColor(light.color, light.temperatureK) * light.luminousFluxLm / (PI * area);

    const float rMax   = InfluenceRadiusIsotropic(light.luminousFluxLm, light.radius);
    const float distSq = dot(P - light.position, P - light.position);
    return (diffuse + specular) * luminance * SmoothDistanceAtt(distSq, 1.0 / max(rMax * rMax, 1e-6));
}

// =============================================================================
// Surface material
// =============================================================================
struct SurfaceMaterial
{
    float3 albedo;
    float  roughness;
    float3 emissive;
    float  metallic;
    float  ao;
    float  dielectricF0; // F0 at metallic 0
};

static const float3 kLodTint[8] =
{
    float3(0.30, 0.85, 0.35),
    float3(0.25, 0.55, 0.95),
    float3(0.95, 0.80, 0.25),
    float3(0.90, 0.35, 0.30),
    float3(0.75, 0.35, 0.90),
    float3(0.25, 0.85, 0.80),
    float3(0.95, 0.55, 0.20),
    float3(0.60, 0.60, 0.60)
};

// Placeholder terrain material until the voxel material path exists: a slope blend driven by the base normal's y
SurfaceMaterial ResolveVoxelMaterial(uint v0, float baseNy)
{
    StructuredBuffer< VoxelChunkDesc > chunks = GetResource(g_Push.ID_VoxelChunkDescs);

    const uint           chunkIndex = VisChunkIndex(v0) - kVoxelChunkInstanceBase;
    const VoxelChunkDesc chunk      = chunks[chunkIndex];

    const float3 surfaceAlbedo = float3(0.45, 0.38, 0.28);
    const float  surfaceRough  = 0.85;
    const float3 cliffAlbedo   = float3(0.30, 0.27, 0.22);
    const float  cliffRough    = 0.95;
    const float  sBlend        = smoothstep(0.35, 0.65, saturate(1.0 - baseNy)); // 0 flat .. 1 vertical

    SurfaceMaterial m;
    m.albedo       = lerp(surfaceAlbedo, cliffAlbedo, sBlend);
    m.roughness    = max(lerp(surfaceRough, cliffRough, sBlend), MIN_ROUGHNESS);
    m.emissive     = float3(0.0);
    m.metallic     = 0.0;
    m.ao           = 1.0;
    m.dielectricF0 = 0.04;

    // chunk tint: distinct color per chunk slot (streaming debug)
    if ((chunk.debugFlags & 1u) != 0u)
    {
        const float3 chunkTint = frac(float3(0.618034, 0.302775, 0.415573) * float(chunkIndex) + float3(0.15, 0.45, 0.75));
        m.albedo = lerp(m.albedo, chunkTint, 0.7);
    }

    // LOD tint: distinct color per terrain LOD level (ring debug)
    if ((chunk.debugFlags & 2u) != 0u)
    {
        const uint lod = chunk.lodAndMask & 0xFFu;
        m.albedo = lerp(m.albedo, kLodTint[min(lod, 7u)], 0.7);
    }

    return m;
}

SurfaceMaterial ResolveMeshMaterial(SceneBufferIndices buffers, float4x4 viewProj, uint v0, uint v1, float2 pixelCenter, float2 viewport, float cachedRoughness)
{
    SurfaceMaterial m;
    m.albedo       = float3(0.0);
    m.roughness    = max(cachedRoughness, MIN_ROUGHNESS);
    m.emissive     = float3(0.0);
    m.metallic     = 0.0;
    m.ao           = 0.0;
    m.dielectricF0 = 0.04;

    const VisTriangle t = FetchVisTriangle(buffers, v0, v1);
    if (t.materialID == kInvalidIndex)
        return m;

    float4 c[3];
    [unroll]
    for (uint k = 0; k < 3; ++k)
        c[k] = mul(viewProj, mul(t.mLocalToWorld, float4(t.position[k], 1.0)));

    const float2 ndc  = PixelCenterToNDC(pixelCenter, viewport);
    const float3 bary = Barycentrics(ndc, c[0], c[1], c[2]);
    const float2 uv   = bary.x * t.uv[0] + bary.y * t.uv[1] + bary.z * t.uv[2];

    float2 ddxUV, ddyUV;
    UVGradient(ndc, 1.0 / viewport, c[0], c[1], c[2], t.uv[0], t.uv[1], t.uv[2], ddxUV, ddyUV);

    StructuredBuffer< MaterialData >        materials        = GetResource(buffers.materials);
    StructuredBuffer< MaterialTextureData > materialTextures = GetResource(buffers.materialTextures);
    const MaterialData material = materials[t.materialID];

    float4 textureValue;
    uint   textureChannel;

    m.albedo = material.tint;
    if (SampleMaterialTextureGrad(materialTextures, material, eMaterialTextureSemantic_BaseColor, uv, ddxUV, ddyUV, textureValue, textureChannel))
        m.albedo *= textureValue.rgb;

    m.metallic = material.metallic;
    if (SampleMaterialTextureGrad(materialTextures, material, eMaterialTextureSemantic_Metallic, uv, ddxUV, ddyUV, textureValue, textureChannel))
        m.metallic *= SelectMaterialTextureScalar(textureValue, textureChannel);

    m.ao = 1.0;
    if (SampleMaterialTextureGrad(materialTextures, material, eMaterialTextureSemantic_Occlusion, uv, ddxUV, ddyUV, textureValue, textureChannel))
        m.ao *= SelectMaterialTextureScalar(textureValue, textureChannel);

    m.emissive = material.emissionColor * material.emissivePower;
    if (SampleMaterialTextureGrad(materialTextures, material, eMaterialTextureSemantic_Emission, uv, ddxUV, ddyUV, textureValue, textureChannel))
        m.emissive *= textureValue.rgb;

    if (material.ior > 1.0)
    {
        const float r = (material.ior - 1.0) / (material.ior + 1.0);
        m.dielectricF0 = r * r;
    }

    return m;
}

float3 DebugVisBary(SceneBufferIndices buffers, float4x4 viewProj, uint v0, uint v1, float2 pixelCenter, float2 viewport)
{
    const VisTriangle t = FetchVisTriangle(buffers, v0, v1);

    float4 c[3];
    [unroll]
    for (uint k = 0; k < 3; ++k)
        c[k] = mul(viewProj, mul(t.mLocalToWorld, float4(t.position[k], 1.0)));

    return Barycentrics(PixelCenterToNDC(pixelCenter, viewport), c[0], c[1], c[2]);
}

// =============================================================================
// Composition
// =============================================================================
float3 ApplyAerialPerspective(float3 color, float2 uv, float viewDistanceM)
{
    Texture3D< float4 > aerialPerspectiveLUT = GetResource(g_Push.ID_AerialPerspectiveLUT);

    uint3 texSize;
    aerialPerspectiveLUT.GetDimensions(texSize.x, texSize.y, texSize.z);

    float slice = viewDistanceM * M_TO_KM / AP_KM_PER_SLICE;
    const float w = sqrt(slice / float(texSize.z));
    slice = w * float(texSize.z);

    float4 ap = aerialPerspectiveLUT.SampleLevel(g_LinearClampSampler, float3(uv, w), 0);

    // prevents an abrupt appearance of fog on objects close to the camera
    const float weight = slice < sqrt(0.5) ? saturate(slice * slice * 2.0) : 1.0;
    ap.rgb *= weight;
    ap.a    = 1.0 - weight * (1.0 - ap.a);

    // FinalColor = (SurfaceColor * Transmittance) + InScatteredLight
    return color * ap.a + ap.rgb;
}

float3 SurfaceDebugColor(uint debugView, uint v0, float3 N, SurfaceMaterial m, SceneBufferIndices buffers, float4x4 viewProj, uint2 px, float2 viewport)
{
    switch (debugView)
    {
    case 1:
        if (VisIsSky(v0))
            return float3(0.0, 0.0, 1.0);
        return VisIsVoxel(v0) ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
    case 2:
        return N * 0.5 + 0.5;
    case 3:
    {
        float3 nLive = N;
        if (VisIsMesh(v0))
        {
            Texture2D< uint > vbuf1 = GetResource(g_Push.ID_VBuf1);
            nLive = ResolveMeshSurface(buffers, viewProj, v0, vbuf1.Load(int3(int2(px), 0)), float2(px) + 0.5, viewport).N;
        }
        return nLive * 0.5 + 0.5;
    }
    case 4:
        return m.albedo;
    case 5:
        return float3(m.roughness);
    case 6:
        return float3(m.ao);
    case 7:
        if (VisIsMesh(v0))
        {
            Texture2D< uint > vbuf1 = GetResource(g_Push.ID_VBuf1);
            return DebugVisBary(buffers, viewProj, v0, vbuf1.Load(int3(int2(px), 0)), float2(px) + 0.5, viewport);
        }
        return float3(0.0);
    default:
        return float3(0.0);
    }
}

[numthreads(16, 16, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2D< float4 > outSceneTexture = GetResource(g_Push.ID_OutSceneTexture);

    uint2 size;
    outSceneTexture.GetDimensions(size.x, size.y);

    const uint2 px = tID.xy;
    if (any(px >= size))
        return;

    StructuredBuffer< SceneGlobals > scene       = GetResource(g_Push.ID_Scene);
    Texture2D< float >               depthBuffer = GetResource(g_Push.ID_DepthBuffer);

    const int3   texel       = int3(int2(px), 0);
    const float2 viewport    = float2(size);
    const float2 pixelCenter = float2(px) + 0.5;
    const float2 uv          = pixelCenter / viewport;
    const float  depth       = depthBuffer.Load(texel);

    float3 color;
    if (depth == 0.0)
    {
        TextureCube< float3 > skyboxLUT = GetResource(g_Push.ID_SkyboxLUT);

        const float3 rayDir = ViewRayDirWS(PixelCenterToNDC(pixelCenter, viewport), scene[0].camera.mProj, scene[0].camera.mView);
        color = skyboxLUT.SampleLevel(g_LinearClampSampler, rayDir, 0);
    }
    else
    {
        Texture2D< uint >   vbuf0        = GetResource(g_Push.ID_VBuf0);
        Texture2D< float2 > coreNormal   = GetResource(g_Push.ID_CoreNormal);
        Texture2D< float4 > coreMaterial = GetResource(g_Push.ID_CoreMaterial);

        const SceneBufferIndices buffers  = scene[0].buffers;
        const float4x4           viewProj = scene[0].camera.mViewProj;

        const uint   v0 = vbuf0.Load(texel);
        const float3 N  = OctDecode(coreNormal.Load(texel));

        // CoreMaterial R: linear roughness for meshes, the resolved base normal's y for voxel terrain
        const float coreR = coreMaterial.Load(texel).r;

        SurfaceMaterial m;
        if (VisIsVoxel(v0))
        {
            m = ResolveVoxelMaterial(v0, coreR);
        }
        else
        {
            Texture2D< uint > vbuf1 = GetResource(g_Push.ID_VBuf1);
            m = ResolveMeshMaterial(buffers, viewProj, v0, vbuf1.Load(texel), pixelCenter, viewport, coreR);
        }

        if (g_Push.constants.debugView != 0u)
        {
            outSceneTexture[px] = float4(SurfaceDebugColor(g_Push.constants.debugView, v0, N, m, buffers, viewProj, px, viewport), 1.0);
            return;
        }

        const float3 posWS          = ReconstructWorldPos(uv, depth, scene[0].camera.mViewProjInv);
        const float3 cameraPosWS    = scene[0].camera.position;
        const float3 V              = normalize(posWS - cameraPosWS);
        const float3 F0             = lerp(float3(m.dielectricF0), m.albedo, m.metallic);

        Texture2D< float4 > ltcMatrix    = GetResource(g_Push.ID_LtcMatrixLUT);
        Texture2D< float4 > ltcAmplitude = GetResource(g_Push.ID_LtcAmplitudeLUT);
        ByteAddressBuffer   lights       = GetResource(buffers.lights);

        const LightCounts counts = LoadLightCounts(lights);

        float3 Lo = float3(0.0);
        [loop]
        for (uint i = 0; i < counts.numDirectionals; ++i)
            Lo += ApplyDirectionalLight(LoadDirectionalLight(lights, i), N, V, m.albedo, m.metallic, m.roughness, F0);

        const float viewZ      = mul(scene[0].frozenCamera.mView, float4(posWS, 1.0)).z;
        const uint  clusterIdx = PixelToClusterIdx(px, viewZ, scene[0].frozenCamera.zNear, scene[0].frozenCamera.zFar, size.x, size.y);

        StructuredBuffer< uint2 > lightGrid     = GetResource(g_Push.ID_LightGridBuffer);
        StructuredBuffer< uint >  lightListData = GetResource(g_Push.ID_LightListDataBuffer);

        const uint2 cell = lightGrid[clusterIdx]; // (offset, count)
        [loop]
        for (uint i = 0; i < cell.y; ++i)
        {
            uint type, idx;
            DecodeLightIndex(lightListData[cell.x + i], type, idx);

            switch (type)
            {
            case LIGHT_TYPE_SPOT:
                Lo += ApplySpotLight(LoadSpotLight(lights, idx), posWS, N, V, m.albedo, m.metallic, m.roughness, F0);
                break;
            case LIGHT_TYPE_SPHERE:
                Lo += ApplySphereLight(LoadSphereLight(lights, idx), posWS, N, V, m.albedo, m.metallic, m.roughness, F0);
                break;
            case LIGHT_TYPE_TUBE:
                Lo += ApplyTubeLight(LoadTubeLight(lights, idx), posWS, N, V, m.albedo, m.metallic, m.roughness, F0);
                break;
            case LIGHT_TYPE_AREA:
                Lo += ApplyAreaLight(LoadAreaLight(lights, idx), posWS, N, V, m.albedo, m.metallic, m.roughness, F0, ltcMatrix, ltcAmplitude);
                break;
            case LIGHT_TYPE_DISK:
                Lo += ApplyDiskLight(LoadDiskLight(lights, idx), posWS, N, V, m.albedo, m.metallic, m.roughness, F0, ltcMatrix, ltcAmplitude);
                break;
            default:
                break;
            }
        }

        // TODO. sky IBL
        color = ApplyAerialPerspective(Lo + m.emissive, uv, length(posWS - cameraPosWS));
    }

    Texture2D< float4 > cloudScatteringLUT = GetResource(g_Push.ID_CloudScatteringLUT);
    const float4 cloud = cloudScatteringLUT.SampleLevel(g_LinearClampSampler, uv, 0);
    color = color * cloud.a + cloud.rgb;

    outSceneTexture[px] = float4(color, 1.0);
}
