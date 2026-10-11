#ifndef _HLSL_PATHUTILS_HEADER
#define _HLSL_PATHUTILS_HEADER

#include "BxDF.hlsli"
#include "MaterialTextures.hlsli"
#include "Medium.hlsli"
#include "Sampling.hlsli"

#define PT_MAX_DEPTH_LIMIT 64u

#define PT_RAY_EPS          1.0e-4

#define PT_BSDF_PRINCIPLED 5u

#define PT_BSDF_FLAG_DIFFUSE      0x1u
#define PT_BSDF_FLAG_GLOSSY       0x2u
#define PT_BSDF_FLAG_TRANSMISSION 0x4u

#define PT_LOBE_EPS 1.0e-4

#define PT_MIN_ROUGHNESS_ALPHA    1.0e-3
#define PT_SMOOTH_ALPHA_THRESHOLD 1.0e-4

// Bounded scene, vacuum exterior
float GetSceneRayLength(float3 originWS, float3 sceneBoundsCenter, float sceneBoundsRadius)
{
    if (!all(isfinite(originWS)) || !all(isfinite(sceneBoundsCenter)) ||
        !isfinite(sceneBoundsRadius) || sceneBoundsRadius <= 0.0)
        return 0.0;

    float sceneDistance = length(originWS - sceneBoundsCenter) + sceneBoundsRadius;
#ifdef _LIGHT
    [loop]
    for (uint i = 0u; i < g_Lights.numSpheres; ++i)
    {
        SphereLight light = g_Lights.spheres[i];
        float3 center = float3(light.posX, light.posY, light.posZ);
        if (all(isfinite(center)) && isfinite(light.radius) && light.radius > 0.0)
            sceneDistance = max(sceneDistance, length(originWS - center) + light.radius);
    }
#endif
    float coordinateScale = max(max(abs(originWS.x), max(abs(originWS.y), abs(originWS.z))), sceneDistance);
    float rayLength = sceneDistance + max(4.0 * PT_RAY_EPS, 1.0e-5 * coordinateScale);
    return isfinite(rayLength) ? rayLength : 0.0;
}

#ifdef _LIGHT
// Shader-local endpoint data; no payload or CPU/GPU record layout changes.
struct AnalyticSphereLightHit
{
    uint sphereIndex;
    float dist;
    float3 normal;
};

bool FindClosestAnalyticSphereLight(RayDesc ray, out AnalyticSphereLightHit hit)
{
    hit = (AnalyticSphereLightHit)0;
    hit.sphereIndex = INVALID_INDEX;
    hit.dist = ray.TMax;
    [loop]
    for (uint i = 0u; i < g_Lights.numSpheres; ++i)
    {
        float distance;
        float3 normal;
        if (IntersectRaySphereLight(ray, g_Lights.spheres[i], distance, normal) &&
            (hit.sphereIndex == INVALID_INDEX || distance < hit.dist))
        {
            hit.sphereIndex = i;
            hit.dist = distance;
            hit.normal = normal;
        }
    }
    return hit.sphereIndex != INVALID_INDEX;
}
#endif

struct SurfacePayload
{
    float2 barycentrics;
    float  dist;
    uint   hitKind;

    uint instanceID;
    uint primitiveID;
};

struct SurfaceData
{
    float3 normal;
    float  dist;

    float3 geometricNormal;
    uint   hitKind;

    float4 tangent; // xyz: world-space tangent, w: authored tangent-frame handedness in world space.

    float2 uv;
    uint   instanceID;
    uint   primitiveID;

    float2 ddxUV;
    float2 ddyUV;
};

bool TryLoadPrimaryMediumState(PrimaryRayMediumStackSeedData seed, out MediumStack hostStack, out MediumStack airStack)
{
    hostStack.count = 0u;
    airStack.Initialize();
    if (seed.status != PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID ||
        seed.count < 1u ||
        seed.count > MAX_MEDIUM_STACK_CAPACITY || seed.airCount > MAX_MEDIUM_STACK_CAPACITY)
    {
        return false;
    }

    [unroll]
    for (uint i = 0u; i < MAX_MEDIUM_STACK_CAPACITY; ++i)
    {
        if (i >= seed.count)
            break;

        PrimaryRayMediumSeedEntryData entry = seed.entries[i];
        if (!IsPathFinite(entry.ior) || entry.ior <= 0.0)
            return false;

        if (i == 0u)
        {
            if (entry.boundaryInstanceID != INVALID_INDEX)
                return false;
        }
        else if (entry.boundaryInstanceID == INVALID_INDEX || entry.mediumID == INVALID_INDEX)
        {
            return false;
        }
    }

    [unroll]
    for (uint i = 0u; i < MAX_MEDIUM_STACK_CAPACITY; ++i)
    {
        if (i >= seed.count)
            break;

        hostStack.entries[i].boundaryInstanceID = seed.entries[i].boundaryInstanceID;
        hostStack.entries[i].medium.mediumID    = seed.entries[i].mediumID;
        hostStack.entries[i].medium.ior         = seed.entries[i].ior;
    }

    for (uint i = 0u; i < seed.airCount; ++i)
    {
        MediumEntry entry;
        entry.boundaryInstanceID = seed.airEntries[i].boundaryInstanceID;
        entry.medium.mediumID    = seed.airEntries[i].mediumID;
        entry.medium.ior         = seed.airEntries[i].ior;
        if (!IsPathFinite(entry.medium.ior) || entry.medium.ior <= 0.0 || !airStack.TryAdd(entry))
            return false;
    }

    hostStack.count = seed.count;
    return true;
}

// Pure (BSDF-less) boundary routing
bool TryCrossPureMediumBoundary(
    uint boundaryInstanceID,
    Medium boundaryMedium,
    uint mediumFlags,
    float NoW,
    inout MediumStack hostStack,
    inout MediumStack airStack)
{
    if (hostStack.count < 1u || hostStack.count > MAX_MEDIUM_STACK_CAPACITY || airStack.count > MAX_MEDIUM_STACK_CAPACITY || 
        boundaryInstanceID == INVALID_INDEX || boundaryMedium.mediumID == INVALID_INDEX || !IsPathFinite(NoW) || abs(NoW) <= EPSILON_MIN)
        return false;

    // Pure boundaries inherit the current host IOR
    boundaryMedium.ior = hostStack.Current().medium.ior;
    if (!IsPathFinite(boundaryMedium.ior) || boundaryMedium.ior <= 0.0)
        return false;

    if ((mediumFlags & MEDIUM_FLAG_AIR_CONTRIBUTOR) != 0u)
    {
        bool bCrossedSurface = NoW < 0.0;
        if (NoW > 0.0)
        {
            MediumEntry entry;
            entry.boundaryInstanceID = boundaryInstanceID;
            entry.medium = boundaryMedium;
            
            return airStack.TryAdd(entry);
        }
        else
        {
            return airStack.TryRemove(boundaryInstanceID);
        }
    }

    // Preserve the already implemented nested replacement-medium transition
    BoundaryMediumPair pair;
    if (!hostStack.TryResolveBoundaryMediums(boundaryInstanceID, boundaryMedium, NoW, pair))
        return false;

    if (pair.isEntering != 0u)
    {
        MediumEntry entry;
        entry.boundaryInstanceID = boundaryInstanceID;
        entry.medium = pair.mediumT;
        return hostStack.TryPush(entry);
    }
    return hostStack.TryPop(boundaryInstanceID);
}

struct ShadowPayload
{
    uint visible;
    uint alphaSeed;
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
    uint   flags;
    float3 weight;      // Sampled-history continuation estimator C_H / q_H.
    uint   valid;
    uint   lobe;
    uint   isDelta;
    uint   attempted;
    float  rrEtaScale;
};

struct RayCone
{
    float radius;
    float tanHalfAngle;
};


bool IsPathFinite3(float3 v)
{
    return all(v == v) && all(abs(v) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38));
}

// MIS weight
float PowerHeuristic(float pdfA, float pdfB)
{
    pdfA = (IsPathFinite(pdfA) && pdfA > 0.0) ? pdfA : 0.0;
    pdfB = (IsPathFinite(pdfB) && pdfB > 0.0) ? pdfB : 0.0;
    float maxPDF = max(pdfA, pdfB);
    if (maxPDF <= 0.0)
        return 0.0;

    float a = pdfA / maxPDF;
    float b = pdfB / maxPDF;
    float a2 = a * a;
    float b2 = b * b;
    return a2 / (a2 + b2);
}

float CalculateMISWeight(float prevMarginalPDF, float pdfLight, uint wasDelta)
{
    if (wasDelta != 0)
        return 1.0;

    if (!IsPathFinite(pdfLight) || pdfLight <= 0.0)
        return 1.0;

    if (!IsPathFinite(prevMarginalPDF) || prevMarginalPDF <= 0.0)
        return 0.0;

    return PowerHeuristic(prevMarginalPDF, pdfLight);
}

float3 ReadBaseColor(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float3 albedo = float3(mat.tintR, mat.tintG, mat.tintB);

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_BASE_COLOR,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        albedo *= textureValue.rgb;
    }

    return max(albedo, float3(0.0, 0.0, 0.0));
}

float ReadOpacityLevel(MaterialData mat, float2 uv)
{
    float opacity = mat.opacity;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureLevel(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_OPACITY,
            uv,
            textureValue,
            channel))
    {
        opacity *= SelectMaterialTextureScalar(textureValue, channel);
    }
    return saturate(opacity);
}


float ReadOpacity(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float opacity = mat.opacity;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_OPACITY,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        opacity *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(opacity);
}

float ReadRoughness(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float roughness = mat.roughness;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_PERCEPTUAL_ROUGHNESS,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        roughness *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(roughness);
}

float ReadMetallic(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float metallic = mat.metallic;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_METALLIC,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        metallic *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(metallic);
}

float ReadOcclusion(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float occlusion = 1.0;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_OCCLUSION,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        occlusion *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(occlusion);
}

float ReadTransmission(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float transmission = mat.transmission;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_TRANSMISSION,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        transmission *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(transmission);
}

float ReadClearcoat(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float clearcoat = mat.clearcoat;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_CLEARCOAT,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        clearcoat *= SelectMaterialTextureScalar(textureValue, channel);
    }

    return saturate(clearcoat);
}

void ReadAnisotropy(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV, out float anisotropy, out float anisotropyRotation)
{
    anisotropy = saturate(mat.anisotropy);

    float rotationSin, rotationCos;
    sincos(mat.anisotropyRotation, rotationSin, rotationCos);
    float2 direction = float2(rotationCos, rotationSin);

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_ANISOTROPY,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        float2 textureDirection = textureValue.rg * 2.0 - 1.0;
        float directionLen2 = dot(textureDirection, textureDirection);
        textureDirection = directionLen2 > EPSILON_MIN
            ? textureDirection * rsqrt(directionLen2)
            : float2(1.0, 0.0);

        direction = float2(
            textureDirection.x * rotationCos - textureDirection.y * rotationSin,
            textureDirection.x * rotationSin + textureDirection.y * rotationCos);
        anisotropy *= textureValue.b;
    }

    anisotropy = saturate(anisotropy);
    anisotropyRotation = atan2(direction.y, direction.x);
}

float3 ReadEmission(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV)
{
    float3 emission =
        float3(mat.emissionColorR, mat.emissionColorG, mat.emissionColorB) *
        mat.emissivePower;

    float4 textureValue;
    uint channel;
    if (SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_EMISSION,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        emission *= textureValue.rgb;
    }

    return max(emission, float3(0.0, 0.0, 0.0));
}

float3 ReadShadingNormal(MaterialData mat, float2 uv, float2 ddxUV, float2 ddyUV, float3 shadingNormalWS, float3 tangentWS, inout float tangentSignWS)
{
    float4 textureValue;
    uint channel;
    if (!SampleMaterialTextureGrad(
            mat,
            MATERIAL_TEXTURE_SEMANTIC_NORMAL,
            uv,
            ddxUV,
            ddyUV,
            textureValue,
            channel))
    {
        return shadingNormalWS;
    }

    float3 normalTS = normalize(textureValue.rgb * 2.0 - 1.0);

    float3 projNfromT = shadingNormalWS * dot(tangentWS, shadingNormalWS);
    tangentWS -= projNfromT; // gram-schmidt

    float tangentWSLen2 = dot(tangentWS, tangentWS);
    if (tangentWSLen2 <= EPSILON_MIN)
        return shadingNormalWS;

    float3 T = tangentWS * rsqrt(tangentWSLen2);
    float3 B = tangentSignWS * cross(shadingNormalWS, T);
    float3 normalWS = normalize(
        normalTS.x * T +
        normalTS.y * B +
        normalTS.z * shadingNormalWS);
    if (dot(normalWS, shadingNormalWS) < 0.0)
    {
        normalWS = -normalWS;
        tangentSignWS = -tangentSignWS;
    }

    return normalWS;
}
float3 OffsetRay(float3 p, float3 n, float3 w)
{
    return p + n * ((dot(n, w) >= 0.0) ? PT_RAY_EPS : -PT_RAY_EPS);
}

bool TraceShadowVisibility(RayDesc ray, uint alphaSeed)
{
#ifdef _LIGHT
    AnalyticSphereLightHit lightHit;
    if (FindClosestAnalyticSphereLight(ray, lightHit))
        return false;
#endif
    ShadowPayload sp;
    sp.visible   = 0u;
    sp.alphaSeed = alphaSeed;
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

// Shadow ray visibility: returns true when no occluder lies between p and target
bool IsVisible(float3 p, float3 n, float3 target, Medium shadowStartMedium, uint alphaSeed)
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

    return TraceShadowVisibility(ray, alphaSeed);
}

#ifdef _MEDIUM
// Prepare the selected outgoing side locally
bool TryPrepareShadowMediumStack(
    MediumStack incidentStack,
    uint boundaryInstanceID,
    BoundaryMediumPair boundaryPair,
    bool canChangePathMedium,
    float3 geometricNormal,
    float3 woWS,
    float3 wiWS,
    out MediumStack shadowStack)
{
    shadowStack = incidentStack;
    if (incidentStack.count < 1u || incidentStack.count > MAX_MEDIUM_STACK_CAPACITY)
        return false;

    float NgoWo = dot(geometricNormal, woWS);
    float NgoWi = dot(geometricNormal, wiWS);
    if (!IsPathFinite(NgoWo) || !IsPathFinite(NgoWi) || abs(NgoWo) <= EPSILON_MIN || abs(NgoWi) <= EPSILON_MIN)
        return false;

    bool bCrossedSurface    = NgoWo * NgoWi < 0.0;
    bool bChangesPathMedium = bCrossedSurface && canChangePathMedium;
    if (!bChangesPathMedium)
        return true;

    if (boundaryPair.isEntering != 0u)
    {
        MediumEntry entry;
        entry.boundaryInstanceID = boundaryInstanceID;
        entry.medium = boundaryPair.mediumT;
        return shadowStack.TryPush(entry);
    }
    return shadowStack.TryPop(boundaryInstanceID);
}

// Shadow traversal only needs triangle identity, alpha and boundary orientation.
bool TryResolveShadowTriangle(SurfacePayload hit, out float2 uv, out float3 geometricNormal)
{
    uv = 0.0;
    geometricNormal = 0.0;

    StructuredBuffer< InstanceData > Instances = GetResource(g_Instances.index);
    StructuredBuffer< MeshData > Meshes = GetResource(g_Meshes.index);
    StructuredBuffer< TransformData > Transforms = GetResource(g_Transforms.index);
    StructuredBuffer< uint > IndexBuffer = GetResource(g_MeshStreams.indices);
    StructuredBuffer< Vertex > VertexBuffer = GetResource(g_MeshStreams.vertices);

    InstanceData instance = Instances[hit.instanceID];
    MeshData mesh = Meshes[instance.meshID];

    uint indexBase = mesh.lods[0].iOffset + hit.primitiveID * 3u;
    Vertex v0 = VertexBuffer[mesh.vOffset + IndexBuffer[indexBase]];
    Vertex v1 = VertexBuffer[mesh.vOffset + IndexBuffer[indexBase + 1u]];
    Vertex v2 = VertexBuffer[mesh.vOffset + IndexBuffer[indexBase + 2u]];
    float3 bary = float3(1.0 - hit.barycentrics.x - hit.barycentrics.y, hit.barycentrics.x, hit.barycentrics.y);

    uv = float2(v0.u, v0.v) * bary.x + float2(v1.u, v1.v) * bary.y + float2(v2.u, v2.v) * bary.z;

    float3 edge01 = float3(v1.posX - v0.posX, v1.posY - v0.posY, v1.posZ - v0.posZ);
    float3 edge02 = float3(v2.posX - v0.posX, v2.posY - v0.posY, v2.posZ - v0.posZ);
    float3 normal = cross(edge01, edge02);

    float normalLength2 = dot(normal, normal);
    float edgeScale2    = dot(edge01, edge01) * dot(edge02, edge02);
    if (!all(isfinite(uv)) || !isfinite(edgeScale2) ||
        !isfinite(normalLength2) || normalLength2 <= edgeScale2 * 1.0e-12)
        return false;

    TransformData transform = Transforms[instance.transformID];

    float3 normalWS = mul(transpose((float3x3)transform.mWorldToLocal), normal * rsqrt(normalLength2));
    float worldNormalLength2 = dot(normalWS, normalWS);
    if (!isfinite(worldNormalLength2) || worldNormalLength2 <= 0.0)
        return false;

    geometricNormal = normalWS * rsqrt(worldNormalLength2);
    return true;
}

// Nearest accepted triangle, rather than the binary accept-first shadow query.
// Reference: https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#inline-raytracing
bool TryTraceShadowSurface(RayDesc ray, uint alphaSeed, out SurfacePayload hit)
{
    hit = (SurfacePayload)0;

    RayQuery< RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES > query;
    query.TraceRayInline(g_Scene, RAY_FLAG_NONE, 0xFF, ray);

    while (query.Proceed())
    {
        if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
            continue;

        SurfacePayload candidate = (SurfacePayload)0;
        candidate.instanceID   = query.CandidateInstanceID();
        candidate.primitiveID  = query.CandidatePrimitiveIndex();
        candidate.barycentrics = query.CandidateTriangleBarycentrics();
        candidate.dist         = query.CandidateTriangleRayT();

        StructuredBuffer< InstanceData > Instances = GetResource(g_Instances.index);
        StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);

        uint materialID = Instances[candidate.instanceID].materialID;
        if (materialID != INVALID_INDEX)
        {
            MaterialData material = Materials[materialID];
            bool isBoundary = (material.materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u;
            bool isMask = (material.materialFlags & MATERIAL_FLAG_ALPHA_MASK) != 0u;
            bool isBlend = (material.materialFlags & MATERIAL_FLAG_ALPHA_BLEND) != 0u;
            if (!isBoundary && (isMask || isBlend))
            {
                float2 uv;
                float3 geometricNormal;
                if (!TryResolveShadowTriangle(candidate, uv, geometricNormal))
                    return false;
                float opacity = ReadOpacityLevel(material, uv);
                if (isMask)
                {
                    if (opacity < material.alphaCutoff)
                        continue;
                }
                else
                {
                    uint opacitySeed = PCGHash(alphaSeed ^
                        PCGHash(candidate.instanceID + 0x9E3779B9u) ^
                        PCGHash(candidate.primitiveID + 0x85EBCA6Bu) ^
                        PCGHash(asuint(candidate.dist) + 0xC2B2AE35u));
                    if (UintToUnitFloat(opacitySeed) >= opacity)
                        continue;
                }
            }
        }
        query.CommitNonOpaqueTriangleHit();
    }

    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        hit.hitKind = 1u;
        hit.dist = query.CommittedRayT();
        hit.instanceID = query.CommittedInstanceID();
        hit.primitiveID = query.CommittedPrimitiveIndex();
        hit.barycentrics = query.CommittedTriangleBarycentrics();
    }
    return true;
}

// Finite boundary-aware connection; each optical interval starts at its exact boundary.
// Reference: https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators#DirectLighting
bool EstimateShadowTransmittance(
    float3 originWS,
    float3 targetWS,
    MediumStack shadowStartStack,
    MediumStack shadowStartAirStack,
    uint alphaSeed,
    inout RngState transmittanceRng,
    out float3 estimatedT)
{
    estimatedT = 0.0;
    if (shadowStartStack.count < 1u || shadowStartStack.count > MAX_MEDIUM_STACK_CAPACITY ||
        shadowStartAirStack.count > MAX_MEDIUM_STACK_CAPACITY)
        return false;

    float3 toTarget = targetWS - originWS;
    float maxLength = length(toTarget);
    if (!isfinite(maxLength) || maxLength <= PT_RAY_EPS)
        return false;

    RayDesc ray;
    ray.Origin    = originWS;
    ray.Direction = toTarget / maxLength;
    ray.TMin      = PT_RAY_EPS;
    ray.TMax      = maxLength - PT_RAY_EPS;

#ifdef _LIGHT
    AnalyticSphereLightHit lightHit;
    if (FindClosestAnalyticSphereLight(ray, lightHit))
        return true; // A valid blocked connection, estimatedT remains zero.
#endif

    MediumStack shadowStack    = shadowStartStack;
    MediumStack shadowAirStack = shadowStartAirStack;

    float3 accumulatedT          = 1.0;
    float  intervalStartDistance = 0.0;

    uint boundaryCrossings = 0u;

    StructuredBuffer< InstanceData > Instances = GetResource(g_Instances.index);
    StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);

    // consider medium changes in ray-traversing.
    // if ray starts at mediumA bounded by mediumB,
    //     T = T_ab(rayOrigin, posMediumABoundary) * T_b(posMediumABoundary, posLightSource)
    [loop]
    for (;;)
    {
        SurfacePayload hit = (SurfacePayload)0;
        if (ray.TMin <= ray.TMax && !TryTraceShadowSurface(ray, alphaSeed, hit))
            return false;

        float  intervalEndDistance = hit.hitKind != 0u ? hit.dist : maxLength;
        float  intervalLength      = intervalEndDistance - intervalStartDistance;
        float3 intervalOriginWS    = ray.Origin + intervalStartDistance * ray.Direction;
        if (intervalLength <= 0.0 || !all(isfinite(intervalOriginWS)))
            return false;

        uint materialID = INVALID_INDEX;
        if (hit.hitKind != 0u)
        {
            materialID = Instances[hit.instanceID].materialID;
            if (materialID == INVALID_INDEX || (Materials[materialID].materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) == 0u)
                return true; // Occlusion fixes T at zero; no medium estimate is needed for this interval.
        }

        Medium intervalMedium = shadowStack.Current().medium;

        MediumStack media; media.Initialize();
        if (shadowStack.count > 1u)
        {
            if (intervalMedium.mediumID != INVALID_INDEX)
                media.Initialize(intervalMedium);
        }
        else if (intervalMedium.mediumID == INVALID_INDEX)
        {
            media = shadowAirStack;
        }
        else
        {
            if (shadowAirStack.count != 0u)
                return false;

            media.Initialize(intervalMedium);
        }

        float3 intervalT = 1.0;
        if (media.count != 0u &&
            !EstimateMediumTransmittance(media, intervalOriginWS, ray.Direction, intervalLength, transmittanceRng, intervalT))
            return false;

        accumulatedT *= intervalT;
        if (!all(isfinite(accumulatedT)) || any(accumulatedT < 0.0))
            return false;

        if (hit.hitKind == 0u)
        {
            estimatedT = accumulatedT;
            return true;
        }

        MaterialData material = Materials[materialID];
        if (++boundaryCrossings > 256u || max(material.layerCount, 1u) != 1u ||
            (material.materialFlags & (MATERIAL_FLAG_ALPHA_MASK | MATERIAL_FLAG_ALPHA_BLEND | MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_RELATIVE_IOR_INTERFACE)) != 0u)
            return false;

        float2 uv;
        float3 geometricNormal;
        if (!TryResolveShadowTriangle(hit, uv, geometricNormal))
            return false;

        Medium toMedium;
        toMedium.mediumID = Instances[hit.instanceID].mediumID;
        toMedium.ior      = intervalMedium.ior;

        MediumData interiorData;
        if (!TryLoadMedium(toMedium.mediumID, interiorData) ||
            !TryCrossPureMediumBoundary(hit.instanceID, toMedium, interiorData.mediumFlags, dot(geometricNormal, -ray.Direction), shadowStack, shadowAirStack))
            return false;

        intervalStartDistance = intervalEndDistance;
        ray.TMin = intervalEndDistance + PT_RAY_EPS;
    }
}

// Reference: https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators#DirectLighting
bool EstimateShadowTransmittance(
    float3 originWS,
    float3 targetWS,
    Medium shadowStartMedium,
    uint alphaSeed,
    inout RngState transmittanceRng,
    out float3 estimatedT)
{
    estimatedT = 0.0;
    float3 toTarget      = targetWS - originWS;
    float  segmentLength = length(toTarget);
    if (!isfinite(segmentLength) || segmentLength <= PT_RAY_EPS)
        return false;

    RayDesc ray;
    ray.Origin    = originWS;
    ray.Direction = toTarget / segmentLength;
    ray.TMin      = PT_RAY_EPS;
    ray.TMax      = max(PT_RAY_EPS, segmentLength - PT_RAY_EPS);

    if (!TraceShadowVisibility(ray, alphaSeed))
        return true;

    if (shadowStartMedium.mediumID == INVALID_INDEX)
    {
        estimatedT = 1.0;
        return true;
    }
    return EstimateMediumTransmittance(shadowStartMedium.mediumID, originWS, ray.Direction, segmentLength, transmittanceRng, estimatedT);
}
// Surface connections start on the selected side of the geometric boundary.
bool EstimateShadowTransmittance(
    float3 p,
    float3 geometricNormal,
    float3 targetWS,
    MediumStack shadowStartStack,
    MediumStack shadowStartAirStack,
    uint alphaSeed,
    inout RngState transmittanceRng,
    out float3 estimatedT)
{
    estimatedT = 0.0;
    float3 toTarget = targetWS - p;
    float distanceSquared = dot(toTarget, toTarget);
    if (!isfinite(distanceSquared) || distanceSquared <= PT_RAY_EPS * PT_RAY_EPS)
        return false;

    float3 wi = toTarget * rsqrt(distanceSquared);
    return EstimateShadowTransmittance(OffsetRay(p, geometricNormal, wi), targetWS,
        shadowStartStack, shadowStartAirStack, alphaSeed, transmittanceRng, estimatedT);
}

bool EstimateShadowTransmittance(
    float3 p,
    float3 geometricNormal,
    float3 targetWS,
    Medium shadowStartMedium,
    uint alphaSeed,
    inout RngState transmittanceRng,
    out float3 estimatedT)
{
    estimatedT = 0.0;
    float3 toTarget = targetWS - p;
    float distanceSquared = dot(toTarget, toTarget);
    if (!isfinite(distanceSquared) || distanceSquared <= PT_RAY_EPS * PT_RAY_EPS)
        return false;

    float3 wi = toTarget * rsqrt(distanceSquared);
    return EstimateShadowTransmittance(OffsetRay(p, geometricNormal, wi), targetWS,
        shadowStartMedium, alphaSeed, transmittanceRng, estimatedT);
}
#endif

#endif // _HLSL_PATHUTILS_HEADER


