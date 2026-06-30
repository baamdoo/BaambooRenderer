#define _CAMERA
#define _MESH
#define _MATERIAL
#include "Common.hlsli"
#include "Sampling.hlsli"

#define RAY_TYPE_PRIMARY 0
#define RAY_TYPE_SHADOW  1
#define NUM_RAY_TYPES    2

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_FrameIndex;
    uint g_AccumReset;
    uint g_NumSamples;
};

ConstantBuffer< DescriptorHeapIndex > g_Skybox      : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_AccumBuffer : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Display     : register(b3, ROOT_CONSTANT_SPACE);

struct PathTracerSettings
{
    uint maxDepth;
    uint padding0;
    uint padding1;
    uint padding2;
};
ConstantBuffer< PathTracerSettings > g_Settings : register(b0, space1);

RaytracingAccelerationStructure g_Scene : register(t0, space1);

struct PathState
{
    float3 beta;
    float  etaScale;
    float3 rayOrigin;
    float  time;
    float3 rayDir;
    uint   depth;
    uint   specularBounce;
    uint   currentMediumID;
};

struct HitPayload
{
    float3 position;
    uint   hitKind;
    float3 normal;
    uint   entering;
    float3 geometricNormal;
    float  relativeEta;
    float3 albedo;
    uint   instanceID;
    float3 emission;
    uint   primitiveID;
};

struct ShadowPayload
{
    uint visible;
};

float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

bool IsFinite3(float3 v)
{
    return !any(isnan(v)) && !any(isinf(v));
}

float3 EvaluateSkyRadiance(float3 dir)
{
    TextureCube< float3 > Skybox = GetResource(g_Skybox.index);
    return Skybox.SampleLevel(g_LinearClampSampler, dir, 0);
}

float OffsetComponent(float p, float n)
{
    const float ORIGIN      = 1.0 / 32.0;
    const float FLOAT_SCALE = 1.0 / 65536.0;
    const float INT_SCALE   = 256.0;

    int offset = (int)(INT_SCALE * n);
    int bits   = asint(p);
    bits += (p < 0.0) ? -offset : offset;

    return abs(p) < ORIGIN ? p + FLOAT_SCALE * n : asfloat(bits);
}

float3 OffsetRay(float3 p, float3 geometricNormal, float3 dir)
{
    float3 n = dot(geometricNormal, dir) >= 0.0 ? geometricNormal : -geometricNormal;
    return float3(
        OffsetComponent(p.x, n.x),
        OffsetComponent(p.y, n.y),
        OffsetComponent(p.z, n.z));
}

float RaySpawnTMin(float3 origin)
{
    float sceneScale = max(max(abs(origin.x), abs(origin.y)), abs(origin.z));
    return max(1.0e-4, sceneScale * 1.0e-6);
}

float3 SampleCosineWorld(float3 normal, float2 u, out float pdf)
{
    float3 localDir;
    SampleHemisphere_Cosine(u, localDir, pdf);

    float3 tangent;
    float3 bitangent;
    BuildONB(normal, tangent, bitangent);
    return normalize(tangent * localDir.x + bitangent * localDir.y + normal * localDir.z);
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

float3 ReadEmission(MaterialData mat, float2 uv)
{
    float3 emission = float3(mat.tintR, mat.tintG, mat.tintB) * mat.emissivePower;
    if (mat.emissivePower > 0.0 && mat.emissiveID != INVALID_INDEX)
    {
        Texture2D emissiveTex = GetResource(mat.emissiveID);
        emission *= emissiveTex.SampleLevel(g_TrilinearWrapSampler, uv, 0).rgb;
    }

    return max(emission, float3(0.0, 0.0, 0.0));
}

[shader("raygeneration")]
void RayGen()
{
    RWTexture2D< float4 > AccumBuf = GetResource(g_AccumBuffer.index);
    RWTexture2D< float4 > Display  = GetResource(g_Display.index);

    uint2 rayIndex      = DispatchRaysIndex().xy;
    uint2 rayDimensions = DispatchRaysDimensions().xy;

    uint sampleCount = max(g_NumSamples, 1u);
    uint maxDepth    = max(g_Settings.maxDepth, 1u);

    float3 radianceSum = float3(0.0, 0.0, 0.0);
    for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        RngState rng = InitRng(rayIndex, g_FrameIndex, sampleIndex);

        float2 pixelSample = 0.5 + SamplePixelTent(NextFloat2(rng));
        float2 uv          = (float2(rayIndex) + pixelSample) / float2(rayDimensions);
        float3 nearTarget  = ReconstructWorldPos(uv, 1.0, g_Camera.mViewProjInv);

        PathState path = (PathState)0;
        path.beta            = float3(1.0, 1.0, 1.0);
        path.etaScale        = 1.0;
        path.rayOrigin       = g_Camera.posWORLD;
        path.rayDir          = normalize(nearTarget - g_Camera.posWORLD);
        path.depth           = 0u;
        path.specularBounce  = 1u;
        path.currentMediumID = INVALID_INDEX;

        float3 L = float3(0.0, 0.0, 0.0);
        [loop] for (uint depth = 0; depth < maxDepth; ++depth)
        {
            RayDesc ray;
            ray.Origin    = path.rayOrigin;
            ray.Direction = path.rayDir;
            ray.TMin      = (depth == 0u) ? g_Camera.zNear : RaySpawnTMin(path.rayOrigin);
            ray.TMax      = g_Camera.zFar;

            HitPayload hp = (HitPayload)0;
            TraceRay(
                g_Scene,
                RAY_FLAG_NONE,
                0xFF,
                RAY_TYPE_PRIMARY,
                NUM_RAY_TYPES,
                0,
                ray,
                hp);

            if (hp.hitKind == 0u)
            {
                L += path.beta * EvaluateSkyRadiance(path.rayDir);
                break;
            }

            L += path.beta * hp.emission;

            float pdf;
            float3 wi = SampleCosineWorld(hp.normal, NextFloat2(rng), pdf);
            float cosTheta = max(dot(hp.normal, wi), 0.0);
            if (pdf <= 0.0 || cosTheta <= 0.0)
                break;

            path.beta *= hp.albedo;
            if (!IsFinite3(path.beta) || !any(path.beta > 0.0))
                break;

            path.rayOrigin = OffsetRay(hp.position, hp.geometricNormal, wi);
            path.rayDir    = wi;
            path.depth     = depth + 1u;
        }

        if (!IsFinite3(L))
            L = float3(0.0, 0.0, 0.0);

        radianceSum += L;
    }

    float3 frameRadiance = radianceSum / float(sampleCount);

    float4 previous = AccumBuf[rayIndex];
    float3 newSum;
    float  newCount;

    if (g_AccumReset != 0u)
    {
        newSum   = frameRadiance;
        newCount = 1.0;
    }
    else
    {
        newSum   = previous.xyz + frameRadiance;
        newCount = previous.w + 1.0;
    }

    AccumBuf[rayIndex] = float4(newSum, newCount);
    Display[rayIndex]  = float4(newSum / max(newCount, 1.0), 1.0);
}


// ───────────────────────────────────────────────────────────────────
// Shadow ray helper (Phase 3 NEE)
// ───────────────────────────────────────────────────────────────────
uint TraceShadowRay(float3 origin, float3 dir, float tMax)
{
    ShadowPayload sp;
    sp.visible = 0u;

    RayDesc r;
    r.Origin    = origin;
    r.Direction = dir;
    r.TMin      = 0.001;
    r.TMax      = tMax;

    TraceRay(
        g_Scene,
        RAY_FLAG_FORCE_NON_OPAQUE
            | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
            | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF,
        RAY_TYPE_SHADOW,    // RayContributionToHitGroupIndex
        NUM_RAY_TYPES,      // MultiplierForGeometryContributionToHitGroupIndex
        1,                  // MissShaderIndex (ShadowMiss)
        r,
        sp);

    return sp.visible;
}

// ───────────────────────────────────────────────────────────────────
// Primary Miss
// ───────────────────────────────────────────────────────────────────
[shader("miss")]
void PrimaryMiss(inout HitPayload hp)
{
    hp.hitKind = 0u;
    // Leaving other fields untouched — RayGen doesn't read them on miss.
}


// ───────────────────────────────────────────────────────────────────
// Shadow Miss / AnyHit
// ───────────────────────────────────────────────────────────────────
[shader("miss")]
void ShadowMiss(inout ShadowPayload sp)
{
    sp.visible = 1u;
}

[shader("anyhit")]
void AnyHit_Shadow(inout ShadowPayload sp, in BuiltInTriangleIntersectionAttributes attr)
{
    AcceptHitAndEndSearch();
}


// ───────────────────────────────────────────────────────────────────
// ClosestHit
// ───────────────────────────────────────────────────────────────────
[shader("closesthit")]
void ClosestHit_Primary(inout HitPayload hp, in BuiltInTriangleIntersectionAttributes attr)
{
    StructuredBuffer< MeshData >     Meshes      = GetResource(g_Meshes.index);
    StructuredBuffer< InstanceData > Instances   = GetResource(g_Instances.index);
    StructuredBuffer< uint >         IndexBuffer = GetResource(g_MeshStreams.indices);
    StructuredBuffer< Vertex >       VertexBuf   = GetResource(g_MeshStreams.vertices);
    StructuredBuffer< MaterialData > Materials   = GetResource(g_Materials.index);

    InstanceData instance = Instances[InstanceID()];
    MeshData     mesh     = Meshes[instance.meshID];
    MaterialData mat      = Materials[instance.materialID];

    uint primIdx   = PrimitiveIndex();
    uint indexBase = mesh.lods[0].iOffset + primIdx * 3u;
    uint i0        = IndexBuffer[indexBase + 0u];
    uint i1        = IndexBuffer[indexBase + 1u];
    uint i2        = IndexBuffer[indexBase + 2u];

    Vertex v0 = VertexBuf[mesh.vOffset + i0];
    Vertex v1 = VertexBuf[mesh.vOffset + i1];
    Vertex v2 = VertexBuf[mesh.vOffset + i2];

    float3 bary = float3(
        1.0 - attr.barycentrics.x - attr.barycentrics.y,
        attr.barycentrics.x,
        attr.barycentrics.y);

    float3 p0OS = float3(v0.posX, v0.posY, v0.posZ);
    float3 p1OS = float3(v1.posX, v1.posY, v1.posZ);
    float3 p2OS = float3(v2.posX, v2.posY, v2.posZ);

    float3 p0World = mul(ObjectToWorld3x4(), float4(p0OS, 1.0));
    float3 p1World = mul(ObjectToWorld3x4(), float4(p1OS, 1.0));
    float3 p2World = mul(ObjectToWorld3x4(), float4(p2OS, 1.0));

    float3 geometricNWorld = cross(p1World - p0World, p2World - p0World);
    float  geomLen2        = dot(geometricNWorld, geometricNWorld);
    geometricNWorld        = (geomLen2 > EPSILON_MIN)
        ? geometricNWorld * rsqrt(geomLen2)
        : float3(0.0, 1.0, 0.0);

    float3 normalOS =
        float3(v0.normalX, v0.normalY, v0.normalZ) * bary.x +
        float3(v1.normalX, v1.normalY, v1.normalZ) * bary.y +
        float3(v2.normalX, v2.normalY, v2.normalZ) * bary.z;

    float3 shadingNWorld = mul(transpose((float3x3)WorldToObject3x4()), normalOS);
    float  shadingLen2   = dot(shadingNWorld, shadingNWorld);
    shadingNWorld        = (shadingLen2 > EPSILON_MIN)
        ? shadingNWorld * rsqrt(shadingLen2)
        : geometricNWorld;

    //if ((mat.materialType & MATERIAL_FLAG_FACE_NORMALS) != 0u)
    //    shadingNWorld = geometricNWorld;

    if (dot(shadingNWorld, geometricNWorld) < 0.0)
        shadingNWorld = -shadingNWorld;

    uint entering = (dot(geometricNWorld, WorldRayDirection()) < 0.0) ? 1u : 0u;

    if (dot(geometricNWorld, WorldRayDirection()) > 0.0)
        geometricNWorld = -geometricNWorld;
    if (dot(shadingNWorld, WorldRayDirection()) > 0.0)
        shadingNWorld = -shadingNWorld;
    if (dot(shadingNWorld, geometricNWorld) < 0.0)
        shadingNWorld = -shadingNWorld;

    float2 uv0 = float2(v0.u, v0.v);
    float2 uv1 = float2(v1.u, v1.v);
    float2 uv2 = float2(v2.u, v2.v);
    float2 uv  = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    float eta = (mat.ior > 0.0) ? mat.ior : 1.0;

    hp.hitKind         = 1u;
    hp.position        = HitWorldPosition();
    hp.normal          = shadingNWorld;
    hp.entering        = entering;
    hp.geometricNormal = geometricNWorld;
    hp.relativeEta     = (entering != 0u) ? eta : (1.0 / max(eta, EPSILON_MIN));
    hp.albedo          = ReadBaseColor(mat, uv);
    hp.instanceID      = InstanceID();
    hp.emission        = ReadEmission(mat, uv);
    hp.primitiveID     = PrimitiveIndex();
}