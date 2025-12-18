#define _CAMERA
#define _LIGHT
#include "Common.hlsli"
#include "AtmosphereCommon.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_Materials            : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_GBuffer0             : register(b2, ROOT_CONSTANT_SPACE);  // Albedo.rgb + AO.a
ConstantBuffer< DescriptorHeapIndex > g_GBuffer1             : register(b3, ROOT_CONSTANT_SPACE);  // Normal.rgb + MaterialID.a
ConstantBuffer< DescriptorHeapIndex > g_GBuffer2             : register(b4, ROOT_CONSTANT_SPACE);  // Emissive.rgb
ConstantBuffer< DescriptorHeapIndex > g_GBuffer3             : register(b5, ROOT_CONSTANT_SPACE);  // MotionVectors.rg + Roughness.b + Metallic.a
ConstantBuffer< DescriptorHeapIndex > g_DepthBuffer          : register(b6, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_AerialPerspectiveLUT : register(b7, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CloudScatteringLUT   : register(b8, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SkyboxLUT            : register(b9, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutSceneTexture      : register(b10, ROOT_CONSTANT_SPACE);

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
    if (light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperatureK);

    float3 kD;
    float3 specular = CalculateBRDF(N, V, L, metallic, roughness, F0, kD);

    float  NoL       = max(dot(N, L), 0.0);
    float3 luminance = lightColor * light.illuminanceLux;

    return (kD * albedo / PI + specular) * luminance * NoL;
}

float3 ApplyPointLight(PointLight light, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    float3 L        = float3(light.posX, light.posY, light.posZ) - P;
    float  distance = length(L);

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperatureK);

    float3 R            = reflect(-V, N);
    float3 centerToRay  = dot(L, R) * R - L;
    float3 closestPoint = L + centerToRay * clamp(light.radiusM / length(centerToRay), 0.0, 1.0);
    L = normalize(closestPoint);

    float3 kD;
    float3 specular = CalculateBRDF(N, V, L, metallic, roughness, F0, kD);

    float  NdotL             = max(dot(N, L), 0.0);
    float  luminousIntensity = light.luminousFluxLm / (light.radiusM * light.radiusM * PI_MUL(4.0));
    float  attenuation       = CalculateAttenuation(distance, light.radiusM);
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
    float cosThetaInner   = cos(light.innerConeAngleRad);
    float cosThetaOuter   = cos(light.outerConeAngleRad);
    float spotAttenuation = clamp((cosTheta - cosThetaOuter) / (cosThetaInner - cosThetaOuter), 0.0, 1.0);

    if (spotAttenuation == 0.0)
        return float3(0.0, 0.0, 0.0);

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperatureK);

    float3 kD;
    float3 specular = CalculateBRDF(N, V, L, metallic, roughness, F0, kD);

    float  NoL               = max(dot(N, L), 0.0);
    float  solidAngle        = PI_MUL(2.0) * (1.0 - cosThetaOuter);
    float  luminousIntensity = light.luminousFluxLm / solidAngle;
    float  attenuation       = CalculateAttenuation(distance, light.radiusM);
    float3 luminance         = lightColor * luminousIntensity * attenuation * spotAttenuation;

    return (kD * albedo / PI + specular) * luminance * NoL;
}

[numthreads(16, 16, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2D< float4 > OutSceneTexture = GetResource(g_OutSceneTexture.index);

    int2 texCoords = tID.xy;

    uint width, height;
    OutSceneTexture.GetDimensions(width, height);

    float2 uv = (float2(texCoords.xy) + 0.5) / float2(width, height);

    Texture2D< float4 > GBuffer0    = GetResource(g_GBuffer0.index);
    Texture2D< float4 > GBuffer1    = GetResource(g_GBuffer1.index);
    Texture2D< float3 > GBuffer2    = GetResource(g_GBuffer2.index);
    Texture2D< float4 > GBuffer3    = GetResource(g_GBuffer3.index);
    Texture2D< float >  DepthBuffer = GetResource(g_DepthBuffer.index);

    float4 gbuffer0 = GBuffer0.SampleLevel(g_LinearClampSampler, uv, 0);
    float4 gbuffer1 = GBuffer1.SampleLevel(g_LinearClampSampler, uv, 0);
    float3 gbuffer2 = GBuffer2.SampleLevel(g_LinearClampSampler, uv, 0);
    float4 gbuffer3 = GBuffer3.SampleLevel(g_LinearClampSampler, uv, 0);

    float3 color = 0.0;
    float  depth = DepthBuffer.SampleLevel(g_PointClampSampler, uv, 0);
    if (depth == 0.0)
    {
        TextureCube< float4 > SkyboxLUT = GetResource(g_SkyboxLUT.index);

        float3 rayDir = normalize(ReconstructWorldPos(uv, 0.0, g_Camera.mViewProjInv));

        color = SkyboxLUT.Sample(g_LinearClampSampler, rayDir).rgb;
    }
    else
    {
        StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);

        float3 albedo = gbuffer0.rgb;
        float  ao     = gbuffer0.a;

        float3 N          = gbuffer1.xyz;
        uint   materialID = (uint)(gbuffer1.w * 255.0);
        float3 emissive   = gbuffer2.rgb;

        float roughness = max(gbuffer3.z, MIN_ROUGHNESS);
        float metallic  = gbuffer3.w;

        float3 posWORLD = ReconstructWorldPos(uv, depth, g_Camera.mViewProjInv);

        float3 V  = normalize(posWORLD);
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

        if (materialID != INVALID_INDEX && materialID < 256)
        {
            MaterialData material = Materials[materialID];
            if (material.ior > 1.0)
            {
                float f0 = pow((material.ior - 1.0) / (material.ior + 1.0), 2.0);
                F0 = float3(f0, f0, f0);
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
        float3 ambient = ambientColor * g_Lights.ambientIntensity * albedo * ao;

        color = ambient + Lo + emissive;

        // Aerial perspective
        {
            Texture3D< float4 > AerialPerspectiveLUT = GetResource(g_AerialPerspectiveLUT.index);

            float viewDistance = max(0.0, length(posWORLD));

            uint3 texSize;
            AerialPerspectiveLUT.GetDimensions(texSize.x, texSize.y, texSize.z);

            float slice = viewDistance * (1.0 / AP_KM_PER_SLICE) * DISTANCE_SCALE;
            float w = sqrt(slice / float(texSize.z));
            slice = w * texSize.z;

            float4 ap = AerialPerspectiveLUT.SampleLevel(g_LinearClampSampler, float3(uv, w), 0);

            // prevents an abrupt appearance of fog on objects close to the camera
            float weight = 1.0;
            if (slice < sqrt(0.5))
            {
                weight = clamp((slice * slice * 2.0), 0.0, 1.0);
            }
            ap.rgb *= weight;
            ap.a = 1.0 - weight * (1.0 - ap.a);

            // FinalColor = (SurfaceColor * Transmittance) + InScatteredLight
            color = color * ap.a + ap.rgb;
        }
    }
    // Cloud
    {
        Texture2D< float4 > CloudScatteringLUT = GetResource(g_CloudScatteringLUT.index);

        float4 cloud = CloudScatteringLUT.SampleLevel(g_LinearClampSampler, uv, 0);

        color = color * cloud.a + cloud.rgb;
    }

    OutSceneTexture[texCoords] = float4(color, 1.0);
}