#define _CAMERA
#include "Common.hlsli"

static const float3 COLOR_TEMPERATURE_LUT[] =
{
    float3(1.000, 0.180, 0.000),  // 1000K
    float3(1.000, 0.390, 0.000),  // 2000K
    float3(1.000, 0.588, 0.275),  // 3000K
    float3(1.000, 0.707, 0.518),  // 4000K
    float3(1.000, 0.792, 0.681),  // 5000K
    float3(1.000, 0.849, 0.818),  // 6000K
    float3(0.949, 0.867, 1.000),  // 7000K
    float3(0.827, 0.808, 1.000),  // 8000K
    float3(0.765, 0.769, 1.000),  // 9000K
    float3(0.726, 0.742, 1.000),  // 10000K
};

static const float MIN_ROUGHNESS = 0.045;

Texture2D                        g_SceneTextures[] : register(t0, space100);
StructuredBuffer< MaterialData > g_Materials       : register(t0, space0);

SamplerState g_Sampler : register(s0, space0);

ConstantBuffer< LightingData > g_Lights : register(b1, space0);
cbuffer RootConstants                   : register(b2, space0)
{
    uint MaterialIndex;
};


struct PSInput
{
    float4 posCLIP      : SV_Position;
    float3 posWORLD     : POSITION;
    float2 uv           : TEXCOORD0;
    float3 normalWORLD  : TEXCOORD1;
    float3 tangentWORLD : TEXCOORD2;
};

float3 ColorTemperatureToRGB(float temperature_K)
{
    float T     = clamp(temperature_K, 1000.0, 10000.0);
    float index = (T - 1000.0) / 1000.0;
    return lerp(COLOR_TEMPERATURE_LUT[(uint)index],
                COLOR_TEMPERATURE_LUT[(uint)index + 1],
                frac(index));
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a    = roughness * roughness;
    float a2   = a * a;
    float NoH  = max(dot(N, H), 0.0);
    float NoH2 = NoH * NoH;

    return a2 / (PI * pow(NoH2 * (a2 - 1.0) + 1.0, 2.0));
}

float GeometrySchlickGGX(float NoV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NoV / (NoV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0);
    return GeometrySchlickGGX(NoL, roughness) * GeometrySchlickGGX(NoV, roughness);
}

float3 CalculateBRDF(float3 N, float3 V, float3 L, float metallic, float roughness, float3 F0, inout float3 kD)
{
    float3 H = normalize(V + L);

    float  D = DistributionGGX(N, H, roughness);
    float  G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 kS = F;
    kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metallic;

    float3 numerator = D * G * F;
    float  denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + EPSILON;

    return numerator / denominator;
}

float CalculateAttenuation(float distance, float lightRadius)
{
    float distance2 = max(distance * distance, lightRadius * lightRadius);
    return 1.0 / distance2;
}

float3 ApplyDirectionalLight(DirectionalLight light, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    float3 L = normalize(float3(-light.dirX, -light.dirY, -light.dirZ));

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperature_K);

    float3 kD;
    float3 specular = CalculateBRDF(N, V, L, metallic, roughness, F0, kD);

    float  NoL = max(dot(N, L), 0.0);
    float3 radiance = lightColor * light.illuminance_lux;

    return (kD * albedo / PI + specular) * radiance * NoL;
}

float3 ApplyPointLight(PointLight light, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    float3 L = float3(light.posX, light.posY, light.posZ) - P;
    float  distance = length(L);

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperature_K);

    float3 R = reflect(-V, N);
    float3 centerToRay = dot(L, R) * R - L;
    float3 closestPoint = L + centerToRay * clamp(light.radius_m / length(centerToRay), 0.0, 1.0);
    L = normalize(closestPoint);

    float3 kD;
    float3 specular = CalculateBRDF(N, V, L, metallic, roughness, F0, kD);

    float  NdotL = max(dot(N, L), 0.0);
    float  luminousIntensity = light.luminousPower_lm / LUMENS_PER_CANDELA;
    float  attenuation = CalculateAttenuation(distance, light.radius_m);
    float3 radiance = lightColor * luminousIntensity * attenuation;

    return (kD * albedo / PI + specular) * radiance * NdotL;
}

float3 ApplySpotLight(SpotLight light, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    float3 L       = float3(light.posX, light.posY, light.posZ) - P;
    float distance = length(L);
    L             /= distance;

    // cone attenuation
    float cosTheta        = dot(L, normalize(float3(-light.dirX, -light.dirY, -light.dirZ)));
    float cosThetaInner   = cos(light.innerConeAngle_rad);
    float cosThetaOuter   = cos(light.outerConeAngle_rad);
    float spotAttenuation = clamp((cosTheta - cosThetaOuter) / (cosThetaInner - cosThetaOuter), 0.0, 1.0);

    if (spotAttenuation == 0.0)
        return float3(0.0, 0.0, 0.0);

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperature_K);

    float3 kD;
    float3 specular = CalculateBRDF(N, V, L, metallic, roughness, F0, kD);

    float  NoL = max(dot(N, L), 0.0);
    float  solidAngle = PI_MUL(2.0) * (1.0 - cosThetaOuter);
    float  luminousIntensity = light.luminousPower_lm / solidAngle;
    float  attenuation = CalculateAttenuation(distance, light.radius_m);
    float3 radiance = lightColor * luminousIntensity * attenuation * spotAttenuation;

    return (kD * albedo / PI + specular) * radiance * NoL;
}

float4 main(PSInput input) : SV_Target
{
    if (MaterialIndex == INVALID_INDEX)
    {
        return float4(1.0, 0.0, 0.0, 1.0);
    }

    MaterialData material = g_Materials[MaterialIndex];

    float3 albedo = float3(1.0, 1.0, 1.0);
    if (material.albedoID != INVALID_INDEX) 
    {
        albedo = g_SceneTextures[NonUniformResourceIndex(material.albedoID)].Sample(g_Sampler, input.uv).rgb;
        albedo = pow(albedo, 2.2);
    }
    albedo *= float3(material.tintR, material.tintG, material.tintB);

    float metallic  = material.metallic;
    float roughness = material.roughness;
    float ao        = 1.0;
    if (material.metallicRoughnessAoID != INVALID_INDEX)
    {
        float3 metallicRoughnessAoSample 
            = g_SceneTextures[NonUniformResourceIndex(material.metallicRoughnessAoID)].Sample(g_Sampler, input.uv).rgb;

        metallic  *= metallicRoughnessAoSample.b;
        roughness *= metallicRoughnessAoSample.g;
        ao        *= metallicRoughnessAoSample.r;
    }
    roughness = max(roughness, MIN_ROUGHNESS);

    float3 N = normalize(input.normalWORLD);
    if (material.normalID != INVALID_INDEX)
    {
        float3 tangentNormal 
            = g_SceneTextures[NonUniformResourceIndex(material.normalID)].Sample(g_Sampler, input.uv).rgb * 2.0 - 1.0;

        float3   T   = normalize(input.tangentWORLD);
        float3   B   = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);

        N = normalize(mul(tangentNormal, TBN));
    }

    float3 V  = normalize(g_Camera.posWORLD - input.posWORLD);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    if (material.ior > 1.0)
    {
        float ior = material.ior;
        float f0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
        F0       = float3(f0, f0, f0);
    }

    float3 Lo = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < g_Lights.numDirectionals; ++i)
    {
        Lo += ApplyDirectionalLight(g_Lights.directionals[i], N, V, albedo, metallic, roughness, F0);
    }

    for (uint i = 0; i < g_Lights.numPoints; ++i)
    {
        Lo += ApplyPointLight(g_Lights.points[i], input.posWORLD, N, V, albedo, metallic, roughness, F0);
    }

    for (uint i = 0; i < g_Lights.numSpots; ++i)
    {
        Lo += ApplySpotLight(g_Lights.spots[i], input.posWORLD, N, V, albedo, metallic, roughness, F0);
    }

    float3 ambientColor = float3(g_Lights.ambientColorR, g_Lights.ambientColorG, g_Lights.ambientColorB);
    float3 ambient      = ambientColor * g_Lights.ambientIntensity * albedo * ao;
    float3 emissive     = float3(0.0, 0.0, 0.0);
    if (material.emissiveID != INVALID_INDEX)
    {
        emissive = g_SceneTextures[NonUniformResourceIndex(material.emissiveID)].Sample(g_Sampler, input.uv).rgb;
        emissive = pow(emissive, 2.2); // convert from sRGB to linear
        emissive *= 10.0;
    }

    float3 color = ambient + Lo + emissive;

    float ev100    = g_Lights.exposure;
    float exposure = 1.0 / (1.2 * pow(2.0, ev100));
    color         *= exposure;

    // tone mapping (ACES filmic)
    float3 x = color;
    float  a = 2.51;
    float  b = 0.03;
    float  c = 2.43;
    float  d = 0.59;
    float  e = 0.14;
    color = clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);

    // gamma correction
    color = pow(color, (1.0 / 2.2));

    return float4(color, 1.0);
}