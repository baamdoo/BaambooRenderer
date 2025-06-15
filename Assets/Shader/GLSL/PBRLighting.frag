#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

#define _CAMERA
#include "Common.hg"

const vec3 COLOR_TEMPERATURE_LUT[] = 
{
    vec3(1.000, 0.180, 0.000),  // 1000K
    vec3(1.000, 0.390, 0.000),  // 2000K
    vec3(1.000, 0.588, 0.275),  // 3000K
    vec3(1.000, 0.707, 0.518),  // 4000K
    vec3(1.000, 0.792, 0.681),  // 5000K
    vec3(1.000, 0.849, 0.818),  // 6000K
    vec3(0.949, 0.867, 1.000),  // 7000K
    vec3(0.827, 0.808, 1.000),  // 8000K
    vec3(0.765, 0.769, 1.000),  // 9000K
    vec3(0.726, 0.742, 1.000),  // 10000K
};

layout(set = SET_STATIC, binding = 0) uniform sampler2D g_SceneTextures[];
layout(set = SET_STATIC, binding = 4) readonly buffer   MaterialBuffer 
{
	MaterialData materials[];
} g_MaterialBuffer;

layout(set = SET_STATIC, binding = 5) readonly buffer LightBuffer
{
	LightingData lightingData;
} g_LightBuffer;

layout(location = 0) in vec3 inPosWORLD;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inNormalWORLD;
layout(location = 3) in vec3 inTangentWORLD;
layout(location = 4) in flat uint inMaterialID;

layout(location = 0) out vec4 outColor;

const float MIN_ROUGHNESS = 0.045;

vec3 ColorTemperatureToRGB(float temperature_K)
{
    float T = clamp(temperature_K, 1000.0, 10000.0);

    float index = (T - 1000.0) / 1000.0;
    return mix(COLOR_TEMPERATURE_LUT[int(index)], 
               COLOR_TEMPERATURE_LUT[int(index) + 1], 
               fract(index));
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a    = roughness * roughness;
    float a2   = a * a;
    float NoH  = max(dot(N, H), 0.0);
    float NoH2 = NoH * NoH;

    float num   = a2;
    float denom = (NoH2 * (a2 - 1.0) + 1.0);
    denom       = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NoV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num   = NoV;
    float denom = NoV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0);
    float ggx1 = GeometrySchlickGGX(NoL, roughness);
    float ggx2 = GeometrySchlickGGX(NoV, roughness);

    return ggx1 * ggx2;
}

vec3 CalculateBRDF(vec3 N, vec3 V, vec3 L, float metallic, float roughness, vec3 F0, inout vec3 kD)
{
    vec3 H = normalize(V + L);

    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3  F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    
    vec3 kS = F;
    kD  = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    vec3  numerator   = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + EPSILON;

    return numerator / denominator;
}

float CalculateAttenuation(float distance, float lightRadius)
{
    float distance2 = max(distance * distance, lightRadius * lightRadius);
    return 1.0 / distance2;
}

vec3 ApplyDirectionalLight(DirectionalLight light, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 L = normalize(vec3(-light.dirX, -light.dirY, -light.dirZ));
    
    vec3 lightColor = vec3(light.colorR, light.colorG, light.colorB);
    if (light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperature_K);
    
    vec3 kD;
    vec3 specular = CalculateBRDF(N, V, L, metallic, roughness, F0, kD);
    
    float NoL      = max(dot(N, L), 0.0);
    vec3  radiance = lightColor * light.illuminance_lux;

    return (kD * albedo / PI + specular) * radiance * NoL;
}

vec3 ApplyPointLight(PointLight light, vec3 P, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3  L        = vec3(light.posX, light.posY, light.posZ) - P;
    float distance = length(L);
    
    vec3 lightColor = vec3(light.colorR, light.colorG, light.colorB);
    if (light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperature_K);
    
    vec3 R            = reflect(-V, N);
    vec3 centerToRay  = dot(L, R) * R - L;
    vec3 closestPoint = L + centerToRay * clamp(light.radius_m / length(centerToRay), 0.0, 1.0);
    L = normalize(closestPoint);
    
    vec3 kD;
    vec3 specular = CalculateBRDF(N, V, L, metallic, roughness, F0, kD);

    float NdotL             = max(dot(N, L), 0.0);
    float luminousIntensity = light.luminousPower_lm / LUMENS_PER_CANDELA;
    float attenuation       = CalculateAttenuation(distance, light.radius_m);
    vec3  radiance          = lightColor * luminousIntensity * attenuation;
    
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

vec3 ApplySpotLight(SpotLight light, vec3 P, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 L         = vec3(light.posX, light.posY, light.posZ) - P;
    float distance = length(L);
    L             /= distance;
    
    // cone attenuation
    float cosTheta        = dot(L, normalize(vec3(-light.dirX, -light.dirY, -light.dirZ)));
    float cosThetaInner   = cos(light.innerConeAngle_rad);
    float cosThetaOuter   = cos(light.outerConeAngle_rad);
    float spotAttenuation = clamp((cosTheta - cosThetaOuter) / (cosThetaInner - cosThetaOuter), 0.0, 1.0);
    
    if (spotAttenuation == 0.0)
        return vec3(0.0);
    
    vec3 lightColor = vec3(light.colorR, light.colorG, light.colorB);
    if (light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperature_K);
    
    vec3 kD;
    vec3 specular = CalculateBRDF(N, V, L, metallic, roughness, F0, kD);
    
    float NoL               = max(dot(N, L), 0.0);
    float solidAngle        = PI_MUL(2.0) * (1.0 - cosThetaOuter);
    float luminousIntensity = light.luminousPower_lm / solidAngle;
    float attenuation       = CalculateAttenuation(distance, light.radius_m);
    vec3  radiance          = lightColor * luminousIntensity * attenuation * spotAttenuation;
    
    return (kD * albedo / PI + specular) * radiance * NoL;
}

void main() 
{
	MaterialData material = g_MaterialBuffer.materials[inMaterialID];

    vec3 albedo = vec3(1.0);
    if (material.albedoID != INVALID_INDEX)
    {
        vec4 albedoSample = texture(g_SceneTextures[nonuniformEXT(material.albedoID)], inUv);

        albedo = pow(albedoSample.rgb, vec3(2.2)); // convert from sRGB to linear
    }
    albedo *= vec3(material.tintR, material.tintG, material.tintB);

    float metallic  = material.metallic;
    float roughness = material.roughness;
    float ao        = 1.0;
    if (material.metallicRoughnessAoID != INVALID_INDEX)
    {
        vec4 metallicRoughnessAoSample = texture(g_SceneTextures[nonuniformEXT(material.metallicRoughnessAoID)], inUv);

        metallic  *= metallicRoughnessAoSample.b;
        roughness *= metallicRoughnessAoSample.g;
        ao        *= metallicRoughnessAoSample.r;
    }
    roughness = max(roughness, MIN_ROUGHNESS);

    vec3 N = normalize(inNormalWORLD);
    if (material.normalID != INVALID_INDEX)
    {
        vec3 tangentNormal = texture(g_SceneTextures[nonuniformEXT(material.normalID)], inUv).xyz * 2.0 - 1.0;

        vec3 T   = normalize(inTangentWORLD);
        vec3 B   = cross(N, T);
        mat3 TBN = mat3(T, B, N);

        N = normalize(TBN * tangentNormal);
    }

    vec3 V  = normalize(g_Camera.posWORLD - inPosWORLD);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    if (material.ior > 1.0)
    {
        float ior = material.ior;
        float f0  = pow((ior - 1.0) / (ior + 1.0), 2.0);
        F0        = vec3(f0);
    }

    vec3 Lo = vec3(0.0);
    for (uint i = 0; i < g_LightBuffer.lightingData.numDirectionals; ++i)
    {
        Lo += ApplyDirectionalLight(g_LightBuffer.lightingData.directionals[i], N, V, albedo, metallic, roughness, F0);
    }
    
    for (uint i = 0; i < g_LightBuffer.lightingData.numPoints; ++i)
    {
        Lo += ApplyPointLight(g_LightBuffer.lightingData.points[i], inPosWORLD, N, V, albedo, metallic, roughness, F0);
    }
    
    for (uint i = 0; i < g_LightBuffer.lightingData.numSpots; ++i)
    {
        Lo += ApplySpotLight(g_LightBuffer.lightingData.spots[i], inPosWORLD, N, V, albedo, metallic, roughness, F0);
    }

    vec3 ambientColor = vec3(g_LightBuffer.lightingData.ambientColorR, g_LightBuffer.lightingData.ambientColorG, g_LightBuffer.lightingData.ambientColorB);
    vec3 ambient      = ambientColor * g_LightBuffer.lightingData.ambientIntensity * albedo * ao;
    vec3 emissive     = vec3(0.0);
    if (material.emissiveID != INVALID_INDEX)
    {
        emissive  = texture(g_SceneTextures[nonuniformEXT(material.emissiveID)], inUv).rgb;
        emissive  = pow(emissive.rgb, vec3(2.2)); // convert from sRGB to linear
        emissive *= 10.0;
    }

    vec3 color = ambient + Lo + emissive;

    // exposure correction
    float ev100    = g_LightBuffer.lightingData.exposure;
    float exposure = 1.0 / (1.2 * pow(2.0, ev100));
    color *= exposure;

    // tone mapping (ACES filmic)
    vec3  x = color;
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    color = clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
    
    // gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}