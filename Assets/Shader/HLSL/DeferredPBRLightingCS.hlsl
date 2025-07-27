#define _CAMERA
#include "Common.hlsli"
#include "AtmosphereCommon.hlsli"

ConstantBuffer< LightingData > g_Lights : register(b1, space0);

Texture2D< float4 > g_GBuffer0             : register(t0); // Albedo.rgb + AO.a
Texture2D< float4 > g_GBuffer1             : register(t1); // Normal.rgb + MaterialID.a
Texture2D< float3 > g_GBuffer2             : register(t2); // Emissive.rgb
Texture2D< float4 > g_GBuffer3             : register(t3); // MotionVectors.rg + Roughness.b + Metallic.a
Texture2D< float >  g_DepthBuffer          : register(t4);
Texture2D< float4 > g_SkyViewLUT           : register(t5);
Texture3D< float4 > g_AerialPerspectiveLUT : register(t6);

StructuredBuffer< MaterialData > g_Materials : register(t7);

RWTexture2D< float4 > g_OutputTexture : register(u0);

SamplerState g_LinearClampSampler : register(s0, space0);

struct PushConstants
{
    float planetRadius_km;
};
ConstantBuffer< PushConstants > g_Push : register(b0, ROOT_CONSTANT_SPACE);

static const float MIN_ROUGHNESS = 0.045;

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

    float3 numerator   = D * G * F;
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

    float  NoL       = max(dot(N, L), 0.0);
    float3 luminance = lightColor * light.illuminance_lux;

    return (kD * albedo / PI + specular) * luminance * NoL;
}

float3 ApplyPointLight(PointLight light, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    float3 L        = float3(light.posX, light.posY, light.posZ) - P;
    float  distance = length(L);

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperature_K);

    float3 R            = reflect(-V, N);
    float3 centerToRay  = dot(L, R) * R - L;
    float3 closestPoint = L + centerToRay * clamp(light.radius_m / length(centerToRay), 0.0, 1.0);
    L = normalize(closestPoint);

    float3 kD;
    float3 specular = CalculateBRDF(N, V, L, metallic, roughness, F0, kD);

    float  NdotL             = max(dot(N, L), 0.0);
    float  luminousIntensity = light.luminousFlux_lm / (light.radius_m * light.radius_m * PI_MUL(4.0));
    float  attenuation       = CalculateAttenuation(distance, light.radius_m);
    float3 luminance         = lightColor * luminousIntensity * attenuation;

    return (kD * albedo / PI + specular) * luminance * NdotL;
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

    float  NoL               = max(dot(N, L), 0.0);
    float  solidAngle        = PI_MUL(2.0) * (1.0 - cosThetaOuter);
    float  luminousIntensity = light.luminousFlux_lm / solidAngle;
    float  attenuation       = CalculateAttenuation(distance, light.radius_m);
    float3 luminance         = lightColor * luminousIntensity * attenuation * spotAttenuation;

    return (kD * albedo / PI + specular) * luminance * NoL;
}

float2 GetUvFromSkyViewRayDirection(float longitude, float latitude, float viewHeight, bool bIntersectGround)
{
    float2 uv;

    float Vhorizon = sqrt(viewHeight * viewHeight - g_Push.planetRadius_km * g_Push.planetRadius_km);
    float cosBeta = Vhorizon / viewHeight;
    float beta = acosFast4(cosBeta);
    float zenithHorizonAngle = PI - beta;

    if (!bIntersectGround)
    {
        float coord = latitude / zenithHorizonAngle;
        coord = 1.0 - coord;
        coord = 1.0 - safeSqrt(coord);

        uv.y = 0.5 * coord;
    }
    else
    {
        float coord = (latitude - zenithHorizonAngle) / beta;
        coord = safeSqrt(coord);

        uv.y = 0.5 + 0.5 * coord;
    }

    {
        uv.x = 0.5 * (longitude + PI) / PI;
    }

    return uv;
}

float3 GetSunLuminance(float3 rayDir, bool bIntersectGround)
{
    float3 L = float3(-g_Lights.directionals[0].dirX, -g_Lights.directionals[0].dirY, -g_Lights.directionals[0].dirZ);
    if (dot(rayDir, L) > cos(0.5 * 0.505 * PI / 180.0))
    {
        if (!bIntersectGround)
        {
            const float3 SunLuminance = 1000000.0;
            return SunLuminance;
        }
    }

    return 0.0;
}

[numthreads(16, 16, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    int2 texCoords = tID.xy;

    uint width, height;
    g_OutputTexture.GetDimensions(width, height);

    float2 uv = (float2(texCoords.xy) + 0.5) / float2(width, height);

    float4 gbuffer0 = g_GBuffer0.SampleLevel(g_LinearClampSampler, uv, 0);
    float4 gbuffer1 = g_GBuffer1.SampleLevel(g_LinearClampSampler, uv, 0);
    float3 gbuffer2 = g_GBuffer2.SampleLevel(g_LinearClampSampler, uv, 0);
    float4 gbuffer3 = g_GBuffer3.SampleLevel(g_LinearClampSampler, uv, 0);

    float  depth    = g_DepthBuffer.SampleLevel(g_LinearClampSampler, uv, 0);
    if (depth == 1.0)
    {
        // sky view
        float2 texSize;
        g_SkyViewLUT.GetDimensions(texSize.x, texSize.y);

        float3 cameraPos =
            float3(g_Camera.posWORLD.x, max(g_Camera.posWORLD.y, MIN_VIEW_HEIGHT_ABOVE_GROUND), g_Camera.posWORLD.z);
        float3 cameraPosAbovePlanet =
            cameraPos * DISTANCE_SCALE + float3(0.0, g_Push.planetRadius_km, 0.0);
        float viewHeight = length(cameraPosAbovePlanet);

        float3 rayDir = normalize(ReconstructWorldPos(uv, 1.0, g_Camera.mViewProjInv));
        float3 rayOrigin = cameraPosAbovePlanet;

        float3  upVec = normalize(rayOrigin);
        float cosLatitude = dot(rayDir, upVec);
        float longitude = atan2Fast(rayDir.z, rayDir.x);

        float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Push.planetRadius_km);
        bool   bIntersectGround   = groundIntersection.x > 0.0;

        float2 skyUV = GetUvFromSkyViewRayDirection(longitude, acosFast4(cosLatitude), viewHeight, bIntersectGround);
        skyUV        = GetUnstretchedTextureUV(skyUV, texSize);
        float4 skyColor = g_SkyViewLUT.SampleLevel(g_LinearClampSampler, skyUV, 0);
        float3 color    = skyColor.rgb + GetSunLuminance(rayDir, bIntersectGround) * skyColor.a;

        // exposure correction
        float ev100    = g_Lights.ev100;
        float exposure = 1.0 / (1.2 * pow(2.0, ev100));
        color *= exposure;

        // tone mapping (ACES filmic)
        float3 x = color;
        float  a = 2.51;
        float  b = 0.03;
        float  c = 2.43;
        float  d = 0.59;
        float  e = 0.14;
        color = clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);

        // gamma correction
        color = pow(color, 1.0 / 2.2);

        g_OutputTexture[texCoords] = vec4(color, 1.0);
        return;
    }

    float3 albedo = gbuffer0.rgb;
    float  ao     = gbuffer0.a;

    float3 N          = gbuffer1.xyz;
    uint   materialID = (uint)(gbuffer1.w * 255.0);
    float3 emissive   = gbuffer2.rgb;

    float roughness = max(gbuffer3.z, MIN_ROUGHNESS);
    float metallic  = gbuffer3.w;

    float3 posWORLD = ReconstructWorldPos(uv, depth, g_Camera.mViewProjInv);

    float3 V  = normalize(g_Camera.posWORLD - posWORLD);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    if (materialID != INVALID_INDEX && materialID < 256) 
    {
        MaterialData material = g_Materials[materialID];
        if (material.ior > 1.0) 
        {
            float f0 = pow((material.ior - 1.0) / (material.ior + 1.0), 2.0);
            F0       = float3(f0, f0, f0);
        }
    }

    float3 Lo = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < g_Lights.numDirectionals; ++i)
    {
        Lo += ApplyDirectionalLight(g_Lights.directionals[i], N, V, albedo, metallic, roughness, F0);
    }

    for (uint i = 0; i < g_Lights.numPoints; ++i)
    {
        Lo += ApplyPointLight(g_Lights.points[i], posWORLD, N, V, albedo, metallic, roughness, F0);
    }

    for (uint i = 0; i < g_Lights.numSpots; ++i)
    {
        Lo += ApplySpotLight(g_Lights.spots[i], posWORLD, N, V, albedo, metallic, roughness, F0);
    }

    float3 ambientColor = float3(g_Lights.ambientColorR, g_Lights.ambientColorG, g_Lights.ambientColorB);
    float3 ambient      = ambientColor * g_Lights.ambientIntensity * albedo * ao;

    float3 color = ambient + Lo + emissive;

    // Aerial perspective
    {
        float viewDistance = max(0.0, length(posWORLD - g_Camera.posWORLD));

        uint3 texSize;
        g_AerialPerspectiveLUT.GetDimensions(texSize.x, texSize.y, texSize.z);

        float slice = viewDistance * (1.0 / AP_KM_PER_SLICE) * DISTANCE_SCALE;
        float w     = sqrt(slice / float(texSize.z));
        slice       = w * texSize.z;

        float4 ap = g_AerialPerspectiveLUT.SampleLevel(g_LinearClampSampler, float3(uv, w), 0);

        // prevents an abrupt appearance of fog on objects close to the camera
        float weight = 1.0;
        if (slice < sqrt(0.5))
        {
            weight = clamp((slice * slice * 2.0), 0.0, 1.0);
        }
        ap.rgb *= weight;
        ap.a    = 1.0 - weight * (1.0 - ap.a);

        // FinalColor = (SurfaceColor * Transmittance) + InScatteredLight
        color = color * ap.a + ap.rgb;
    }

    float ev100    = g_Lights.ev100;
    float exposure = 1.0 / (1.2 * pow(2.0, ev100));
    color *= exposure;

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

    g_OutputTexture[texCoords] = float4(color, 1.0);
}