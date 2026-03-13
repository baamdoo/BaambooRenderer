#define _CAMERA
#define _MESH
#define _LIGHT
#define _MATERIAL
#include "BxDf.hlsli"
#include "HelperFunctions.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_FrameIndex;
    float g_TimeSec;
};

ConstantBuffer< DescriptorHeapIndex > g_Skybox : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Output : register(b2, ROOT_CONSTANT_SPACE);

RaytracingAccelerationStructure g_Scene : register(t0, space1);


// ───────────────────────────────────────────────────────────────────
// Configuration
// ───────────────────────────────────────────────────────────────────
#define NUM_SHADOW_SAMPLES_DIRECTIONAL 8
#define NUM_SHADOW_SAMPLES_POINT       4
#define NUM_SHADOW_SAMPLES_SPOT        4
                                       
#define MAX_RADIANCE_RECURSION_DEPTH 3

#define RAY_TYPE_RADIANCE 0
#define RAY_TYPE_SHADOW   1
#define NUM_RAY_TYPES     2


// ───────────────────────────────────────────────────────────────────
// Types
// ───────────────────────────────────────────────────────────────────
struct Ray
{
    float3 origin;
    float3 direction;
};

struct ShadowPayload
{
    float visibility;
};

struct RadiancePayload
{
    float3 radiance;
    float  depth;
    uint   rayRecursionDepth;
};


// ───────────────────────────────────────────────────────────────────
// Helper Functions
// ───────────────────────────────────────────────────────────────────
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

float3 OffsetRay(float3 p, float3 n)
{
    return p + n * 0.001;
}

uint PCGHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

BxDF::MaterialParams ReadMaterial(MaterialData material, float2 uv)
{
    BxDF::MaterialParams params = (BxDF::MaterialParams)0;

    // ──────────────── Base Color ────────────────
    float3 albedo = float3(material.tintR, material.tintG, material.tintB);
    if (material.albedoID != INVALID_INDEX)
    {
        Texture2D albedoTex = GetResource(material.albedoID);
        albedo *= albedoTex.SampleLevel(g_LinearWrapSampler, uv, 0).rgb;
    }
    params.albedo = albedo;

    // ──────────────── Roughness / Metallic ────────────────
    float roughness = max(material.roughness, 0.045);
    float metallic  = material.metallic;
    if (material.metallicRoughnessAoID != INVALID_INDEX)
    {
        Texture2D ormTex = GetResource(material.metallicRoughnessAoID);

        float4 orm = ormTex.SampleLevel(g_LinearWrapSampler, uv, 0);
        roughness = max(orm.g * roughness, 0.045);
        metallic  = orm.b * metallic;
    }
    params.roughness = roughness;
    params.metallic  = metallic;

    // ──────────────── F0 (Specular override + IOR) ────────────────
    params.F0 = BxDF::ComputeF0(albedo, metallic, material.ior);

    // ──────────────── Specular ────────────────
    params.specularStrength = material.specularStrength;

    // ──────────────── Clearcoat ────────────────
    params.clearcoat = material.clearcoat;
    params.clearcoatRoughness = max(material.clearcoatRoughness, 0.045);

    if (material.clearcoatID != INVALID_INDEX)
    {
        Texture2D ccTex = GetResource(material.clearcoatID);

        float2 ccSample = ccTex.SampleLevel(g_LinearWrapSampler, uv, 0).rg;
        params.clearcoat          *= ccSample.r;
        params.clearcoatRoughness *= ccSample.g;
    }

    // ──────────────── Anisotropy ────────────────
    params.anisotropy = material.anisotropy;
    params.anisotropyRotation = material.anisotropyRotation;

    if (material.anisotropyID != INVALID_INDEX)
    {
        Texture2D anisoTex = GetResource(material.anisotropyID);

        float3 anisoSample = anisoTex.SampleLevel(g_LinearWrapSampler, uv, 0).rgb;
        params.anisotropy *= anisoSample.b;
        float2 anisoDir = anisoSample.rg * 2.0 - 1.0;
        params.anisotropyRotation = atan2(anisoDir.y, anisoDir.x);
    }

    // ──────────────── Sheen ────────────────
    params.sheenColor = float3(material.sheenColorR, material.sheenColorG, material.sheenColorB);
    params.sheenRoughness = material.sheenRoughness;

    if (material.sheenID != INVALID_INDEX)
    {
        Texture2D sheenTex = GetResource(material.sheenID);

        float4 sheenSample = sheenTex.SampleLevel(g_LinearWrapSampler, uv, 0);
        params.sheenColor *= sheenSample.rgb;
        params.sheenRoughness *= sheenSample.a;
    }

    // ──────────────── Subsurface ────────────────
    params.subsurface = material.subsurface;

    if (material.subsurfaceID != INVALID_INDEX)
    {
        Texture2D sssTex = GetResource(material.subsurfaceID);
        params.subsurface *= sssTex.SampleLevel(g_LinearWrapSampler, uv, 0).r;
    }

    return params;
}

bool ComputeRefraction(float3 I, float3 N, float eta, out float3 refracted)
{
    float cosThetaIn    = dot(-I, N);
    float sinThetaInSq  = max(0.0, 1.0 - cosThetaIn * cosThetaIn);
    float sinThetaOutSq = eta * eta * sinThetaInSq;
    if (sinThetaOutSq >= 1.0)
    { 
        refracted = float3(0, 0, 0); 
        return false; 
    }
    float cosThetaOut = sqrt(1.0 - sinThetaOutSq);

    refracted = normalize(eta * I + (eta * cosThetaIn - cosThetaOut) * N);
    return true;
}

float3 GetLightColor(float3 baseColor, float temperatureK)
{
    float3 color = baseColor;
    if (temperatureK > 0.0)
        color *= ColorTemperatureToRGB(temperatureK);
    return color;
}

float TraceShadowRay(float3 hitPosition, float3 N, float3 lightDir, float tMax)
{
    ShadowPayload shadowPayload;
    shadowPayload.visibility = 1.0;

    RayDesc shadowRay;
    shadowRay.Origin    = OffsetRay(hitPosition, N);
    shadowRay.Direction = lightDir;
    shadowRay.TMin      = 0.001;
    shadowRay.TMax      = tMax;

    TraceRay(
        g_Scene,
        RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        ~0,
        RAY_TYPE_SHADOW,
        NUM_RAY_TYPES,
        1,
        shadowRay,
        shadowPayload);

    return shadowPayload.visibility;
}

RadiancePayload TraceRadianceRay(float3 origin, float3 direction, uint currentDepth)
{
    RadiancePayload p = (RadiancePayload)0;
    if (currentDepth >= MAX_RADIANCE_RECURSION_DEPTH)
    {
        TextureCube< float3 > Skybox = GetResource(g_Skybox.index);
        float3 skyColor = Skybox.SampleLevel(g_LinearClampSampler, direction, 0);

        p.radiance = skyColor;
        return p;
    }
    p.rayRecursionDepth = currentDepth;
    RayDesc r; r.Origin = origin; r.Direction = direction; r.TMin = 0.001; r.TMax = g_Camera.zFar;
    TraceRay(g_Scene, 0, 0xFF, RAY_TYPE_RADIANCE, NUM_RAY_TYPES, 0, r, p);
    return p;
}

float3 EvaluateEnvReflection(float3 hitPosition, float3 N, float3 V, BxDF::MaterialParams mp, float ao, uint curDepth)
{
    float3 Lenv = float3(0, 0, 0);
    if (curDepth > MAX_RADIANCE_RECURSION_DEPTH)
        return Lenv;

    float NoV = saturate(dot(N, V));

    float3 Fr         = BxDF::Fresnel(mp.F0, NoV);
    float  maxFr      = max(Fr.r, max(Fr.g, Fr.b));
    float  smoothness = 1.0 - mp.roughness * mp.roughness;
    float  importance = maxFr * smoothness;
    if (importance > 0.02)
    {
        float3 R        = reflect(-V, N);
        float3 radiance = TraceRadianceRay(OffsetRay(hitPosition, N), R, curDepth).radiance;

        Lenv = Fr * radiance * smoothness * ao;
    }

    return Lenv;
}

float3 EvaluateDirectLighting(float3 hitPosition, float3 N, float3 V, float3 T, float3 B, BxDF::MaterialParams mp)
{
    float3 Lo = float3(0, 0, 0);

    for (uint di = 0; di < g_Lights.numDirectionals; ++di)
    {
        DirectionalLight light = g_Lights.directionals[di];

        float3 L   = normalize(float3(-light.dirX, -light.dirY, -light.dirZ));
        float  NoL = dot(N, L);
        if (NoL <= 0.0 && mp.subsurface <= 0.0)
            continue;

        float shadowFactor = TraceShadowRay(hitPosition, N, L, g_Camera.zFar);
        if (shadowFactor <= 0.0)
            continue;

        float3 lightColor = GetLightColor(float3(light.colorR, light.colorG, light.colorB), light.temperatureK);
        float3 luminance  = lightColor * light.illuminanceLux;

        Lo += shadowFactor * luminance * BxDF::Evaluate(mp, N, V, L, T, B);
    }

    for (uint pi = 0; pi < g_Lights.numPoints; ++pi)
    {
        PointLight light = g_Lights.points[pi];

        float3 lightPos = float3(light.posX, light.posY, light.posZ);
        float3 toLight  = lightPos - hitPosition;
        float  dist     = length(toLight);
        float3 L        = toLight / dist;
        
        float NoL = dot(N, L);
        if (NoL <= 0.0 && mp.subsurface <= 0.0)
            continue;

        float shadowFactor = TraceShadowRay(hitPosition, N, L, g_Camera.zFar);
        if (shadowFactor <= 0.0)
            continue;

        float3 lightColor        = GetLightColor(float3(light.colorR, light.colorG, light.colorB), light.temperatureK);
        float  luminousIntensity = light.luminousFluxLm / (light.radiusM * light.radiusM * PI_MUL(4.0));

        float  attenuation = 1.0 / max(dist * dist, light.radiusM * light.radiusM);
        float3 luminance   = lightColor * luminousIntensity * attenuation;

        Lo += shadowFactor * luminance * BxDF::Evaluate(mp, N, V, L, T, B);
    }

    for (uint si = 0; si < g_Lights.numSpots; ++si)
    {
        SpotLight light = g_Lights.spots[si];

        float3 lightPos = float3(light.posX, light.posY, light.posZ);
        float3 toLight  = lightPos - hitPosition;
        float  dist     = length(toLight);
        float3 L        = toLight / dist;

        float3 spotDir         = normalize(float3(-light.dirX, -light.dirY, -light.dirZ));
        float  cosTheta        = dot(L, spotDir);
        float  cosThetaInner   = cos(light.innerConeAngleRad);
        float  cosThetaOuter   = cos(light.outerConeAngleRad);
        float  spotAttenuation = saturate((cosTheta - cosThetaOuter) / (cosThetaInner - cosThetaOuter));

        if (spotAttenuation <= 0.0)
            continue;

        float NoL = dot(N, L);
        if (NoL <= 0.0 && mp.subsurface <= 0.0)
            continue;

        float shadowFactor = TraceShadowRay(hitPosition, N, L, g_Camera.zFar);
        if (shadowFactor <= 0.0)
            continue;

        float3 lightColor = GetLightColor(float3(light.colorR, light.colorG, light.colorB), light.temperatureK);
        float  solidAngle = PI_MUL(2.0) * (1.0 - cosThetaOuter);

        float  luminousIntensity = light.luminousFluxLm / solidAngle;
        float  attenuation       = 1.0 / max(dist * dist, light.radiusM * light.radiusM);
        float3 luminance         = lightColor * luminousIntensity * attenuation * spotAttenuation;

        Lo += shadowFactor * luminance * BxDF::Evaluate(mp, N, V, L, T, B);
    }

    return Lo;
}

// ───────────────────────────────────────────────────────────────────
// Ray Generation
// ───────────────────────────────────────────────────────────────────
[shader("raygeneration")]
void RayGen()
{
    RWTexture2D< float4 > Output = GetResource(g_Output.index);

    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim   = DispatchRaysDimensions().xy;

    float2 uv = (float2(launchIndex) + 0.5) / float2(launchDim);

    float3 target    = ReconstructWorldPos(uv, 0.0, g_Camera.mViewProjInv); // reversed-Z
    float3 rayDir    = normalize(target.xyz - g_Camera.posWORLD);
    float3 rayOrigin = g_Camera.posWORLD;

    RayDesc ray;
    ray.Origin    = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin      = g_Camera.zNear;
    ray.TMax      = g_Camera.zFar;

    RadiancePayload payload = { float3(0.0, 0.0, 0.0), 0.0, 0 };

    TraceRay(
        g_Scene,
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
        0xFF,
        RAY_TYPE_RADIANCE,
        NUM_RAY_TYPES,
        0,
        ray,
        payload
    );

    Output[launchIndex] = float4(payload.radiance, 1.0);
}


// ───────────────────────────────────────────────────────────────────
// Miss
// ───────────────────────────────────────────────────────────────────
[shader("miss")]
void RadianceMiss(inout RadiancePayload payload)
{
    float3 dir = normalize(WorldRayDirection());
    
    TextureCube< float3 > Skybox = GetResource(g_Skybox.index);
    float3 skyColor = Skybox.SampleLevel(g_LinearClampSampler, dir, 0);

    payload.radiance = skyColor;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    // No modification needed.
    // If no opaque surface was hit, visibility retains the accumulated
    // value from all transmissive surfaces the ray passed through.
}


// ───────────────────────────────────────────────────────────────────
// Any Hit
// ───────────────────────────────────────────────────────────────────
[shader("anyhit")]
void RadianceAnyHit(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    StructuredBuffer< InstanceData > InstanceBuffer = GetResource(g_Instances.index);
    uint instanceID = InstanceID();
    InstanceData instance = InstanceBuffer[instanceID];

    uint primitiveIndex = PrimitiveIndex();
    uint indexOffset    = instance.iOffset + (primitiveIndex * 3);

    StructuredBuffer< uint > IndexBuffer = GetResource(g_Indices.index);
    uint i0 = IndexBuffer[indexOffset + 0];
    uint i1 = IndexBuffer[indexOffset + 1];
    uint i2 = IndexBuffer[indexOffset + 2];

    StructuredBuffer< Vertex > VertexBuffer = GetResource(g_Vertices.index);
    Vertex v0 = VertexBuffer[instance.vOffset + i0];
    Vertex v1 = VertexBuffer[instance.vOffset + i1];
    Vertex v2 = VertexBuffer[instance.vOffset + i2];

    StructuredBuffer< MaterialData > MaterialBuffer = GetResource(g_Materials.index);
    MaterialData material = MaterialBuffer[instance.materialID];

    float3 barycentrics = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv =
        float2(v0.u, v0.v) * barycentrics.x +
        float2(v1.u, v1.v) * barycentrics.y +
        float2(v2.u, v2.v) * barycentrics.z;

    if (material.alphaCutoff > 0.0 && material.albedoID != INVALID_INDEX)
    {
        Texture2D albedoTex = GetResource(material.albedoID);

        float alpha = albedoTex.SampleLevel(g_LinearClampSampler, uv, 0).a;
        if (alpha < material.alphaCutoff)
        {
            IgnoreHit();
        }
    }
}

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    StructuredBuffer< InstanceData > Instances = GetResource(g_Instances.index);
    InstanceData instance = Instances[InstanceID()];

    uint primitiveIndex = PrimitiveIndex();
    uint indexOffset    = instance.iOffset + (primitiveIndex * 3);

    StructuredBuffer< uint > IndexBuffer = GetResource(g_Indices.index);
    uint i0 = IndexBuffer[indexOffset + 0];
    uint i1 = IndexBuffer[indexOffset + 1];
    uint i2 = IndexBuffer[indexOffset + 2];

    StructuredBuffer< Vertex > VertexBuffer = GetResource(g_Vertices.index);
    Vertex v0 = VertexBuffer[instance.vOffset + i0];
    Vertex v1 = VertexBuffer[instance.vOffset + i1];
    Vertex v2 = VertexBuffer[instance.vOffset + i2];

    StructuredBuffer< MaterialData > MaterialBuffer = GetResource(g_Materials.index);
    MaterialData material = MaterialBuffer[instance.materialID];

    if (material.alphaCutoff > 0.0 && material.albedoID != INVALID_INDEX)
    {
        float3 barycentrics = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
        float2 uv =
            float2(v0.u, v0.v) * barycentrics.x +
            float2(v1.u, v1.v) * barycentrics.y +
            float2(v2.u, v2.v) * barycentrics.z;

        Texture2D tex = GetResource(material.albedoID);
        if (tex.SampleLevel(g_LinearWrapSampler, uv, 0).a < material.alphaCutoff)
        {
            IgnoreHit();
            return;
        }
    }

    float transmission = material.transmission;
    if (transmission > 0.0)
    {
        float tintLuminance = ConvertColorToLuminance(float3(material.tintR, material.tintG, material.tintB));
        float transmittance = transmission * lerp(1.0, tintLuminance, transmission);

        payload.visibility *= transmittance;
        if (payload.visibility > 0.01)
        {
            IgnoreHit();
            return;
        }
    }

    // Shadow on opaque surface => fully blocks light
    payload.visibility = 0.0;
    AcceptHitAndEndSearch();
}


// ───────────────────────────────────────────────────────────────────
// Closest Hit
// ───────────────────────────────────────────────────────────────────
[shader("closesthit")]
void ClosestHit(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    StructuredBuffer< InstanceData > InstanceBuffer = GetResource(g_Instances.index);
    uint instanceID = InstanceID();
    InstanceData instance = InstanceBuffer[instanceID];

    uint primitiveIndex = PrimitiveIndex();
    uint indexOffset    = instance.iOffset + (primitiveIndex * 3);

    StructuredBuffer< uint > IndexBuffer = GetResource(g_Indices.index);
    uint i0 = IndexBuffer[indexOffset + 0];
    uint i1 = IndexBuffer[indexOffset + 1];
    uint i2 = IndexBuffer[indexOffset + 2];

    StructuredBuffer< Vertex > VertexBuffer = GetResource(g_Vertices.index);
    Vertex v0 = VertexBuffer[instance.vOffset + i0];
    Vertex v1 = VertexBuffer[instance.vOffset + i1];
    Vertex v2 = VertexBuffer[instance.vOffset + i2];

    StructuredBuffer< MaterialData > MaterialBuffer = GetResource(g_Materials.index);
    MaterialData material = MaterialBuffer[instance.materialID];

    float3 barycentrics = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv = 
        float2(v0.u, v0.v) * barycentrics.x + 
        float2(v1.u, v1.v) * barycentrics.y + 
        float2(v2.u, v2.v) * barycentrics.z;

    float3 normalOS = 
        float3(v0.normalX, v0.normalY, v0.normalZ) * barycentrics.x + 
        float3(v1.normalX, v1.normalY, v1.normalZ) * barycentrics.y +
        float3(v2.normalX, v2.normalY, v2.normalZ) * barycentrics.z;
    float3 tangentOS =
        float3(v0.tangentX, v0.tangentY, v0.tangentZ) * barycentrics.x +
        float3(v1.tangentX, v1.tangentY, v1.tangentZ) * barycentrics.y +
        float3(v2.tangentX, v2.tangentY, v2.tangentZ) * barycentrics.z;

    float3 N = normalize(mul((float3x3)ObjectToWorld3x4(), normalOS));
    float3 T = normalize(mul((float3x3)ObjectToWorld3x4(), tangentOS));
    float3 B = cross(N, T);

    if (material.normalID != INVALID_INDEX)
    {
        Texture2D normalMap = GetResource(material.normalID);
        float3 normalSample = normalMap.SampleLevel(g_LinearWrapSampler, uv, 0).rgb * 2.0 - 1.0;

        float3x3 TBN = float3x3(T, B, N);

        N = normalize(mul(normalSample, TBN));
        T = normalize(T - N * dot(N, T));
        B = cross(N, T);
    }

    float3 V           = normalize(-WorldRayDirection());
    float3 I           = -V;
    float3 hitPosition = HitWorldPosition();

    BxDF::MaterialParams matParams = ReadMaterial(material, uv);

    float ao = 1.0;
    if (material.metallicRoughnessAoID != INVALID_INDEX)
    {
        Texture2D ormTex = GetResource(material.metallicRoughnessAoID);
        ao = ormTex.SampleLevel(g_LinearWrapSampler, uv, 0).r;
    }

    float ior          = max(material.ior, 1.0);
    float transmission = material.transmission;
    uint  curDepth     = payload.rayRecursionDepth + 1;
    
    float3 ambientColor = float3(g_Lights.ambientColorR, g_Lights.ambientColorG, g_Lights.ambientColorB);

    // ════════════════════════════════════════════════════════════════
    // TRANSMISSIVE PATH
    // ════════════════════════════════════════════════════════════════
    if (transmission > 0.0)
    {
        bool   bEntering = dot(N, I) < 0.0;
        float3 faceN     = bEntering ? N : -N;

        // Fresnel: how much light reflects vs transmits
        float  NoV = saturate(dot(V, faceN));
        float3 Fr  = BxDF::Fresnel(matParams.F0, NoV);

        float kS = Fr.x;
        float kD = 1.0 - Fr.x;

        float3 reflection = float3(0, 0, 0);
        if (curDepth <= MAX_RADIANCE_RECURSION_DEPTH)
        {
            float3 R   = reflect(V, faceN);
            reflection = TraceRadianceRay(OffsetRay(hitPosition, faceN), R, curDepth).radiance;
        }

        float  eta        = bEntering ? (1.0 / ior) : ior; // ni / no
        float3 refraction = float3(0, 0, 0);

        float3 refractedDir;
        if (!ComputeRefraction(I, faceN, eta, refractedDir))
        {
            // total internal reflection
            kS = 1.0;
            kD = 0.0;
        }
        else if (curDepth <= MAX_RADIANCE_RECURSION_DEPTH)
        {
            refraction = TraceRadianceRay(OffsetRay(hitPosition, -faceN), refractedDir, curDepth).radiance;
        }

        if (!bEntering)
        {
            float  travelDist  = length(hitPosition - WorldRayOrigin());
            float3 absCoeff    = -log(max(matParams.albedo, 1e-3));
            float3 attenuation = exp(-absCoeff * travelDist * 0.5);

            reflection *= attenuation;
            refraction *= attenuation;
        }
        float3 glassColor = kS * reflection + kD * refraction;

        float transFactor = saturate(transmission);
        if (transFactor < 1.0)
        {
            float3 Lo = EvaluateDirectLighting(hitPosition, N, V, T, B, matParams);
            float3 Le = EvaluateEnvReflection(hitPosition, N, V, matParams, ao, curDepth);
            float3 La = ambientColor * g_Lights.ambientIntensity * matParams.albedo * ao;

            glassColor = lerp(La + Lo + Le, glassColor, transFactor);
        }

        float3 emissive = float3(0, 0, 0);
        if (material.emissivePower > 0.0 && material.emissiveID != INVALID_INDEX)
        {
            Texture2D emissiveTex = GetResource(material.emissiveID);
            emissive = emissiveTex.SampleLevel(g_LinearWrapSampler, uv, 0).rgb * material.emissivePower;
        }

        payload.depth    = length(hitPosition - WorldRayOrigin());
        payload.radiance = glassColor + emissive;
        return;
    }


    // ════════════════════════════════════════════════════════════════
    // OPAQUE PATH
    // ════════════════════════════════════════════════════════════════

    float3 Lo   = EvaluateDirectLighting(hitPosition, N, V, T, B, matParams);
    float3 La   = ambientColor * g_Lights.ambientIntensity * matParams.albedo * ao;
    float3 Lenv = EvaluateEnvReflection(hitPosition, N, V, matParams, ao, curDepth);

    float3 Le = float3(0.0, 0.0, 0.0);
    if (material.emissivePower > 0.0 && material.emissiveID != INVALID_INDEX)
    {
        Texture2D emissiveTex = GetResource(material.emissiveID);
        Le = emissiveTex.SampleLevel(g_LinearWrapSampler, uv, 0).rgb * material.emissivePower;
    }

    payload.depth    = length(hitPosition - WorldRayOrigin());
    payload.radiance = La + Lo + Lenv + Le;
}
