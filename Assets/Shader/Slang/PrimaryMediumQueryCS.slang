#include "ShaderShared/Scene.h"
#include "ShaderShared/Medium.h"
#include "Common/Binding.slangh"
#include "Common/PathUtils.slangh"

struct PrimaryMediumQueryPush
{
    PrimaryMediumQueryParams constants;

    uint ID_Scene;
    uint ID_MaterialSlabs;
    uint ID_PrimaryRayMediumSeedOut;
};

[[vk::push_constant]] ConstantBuffer< PrimaryMediumQueryPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

static const uint PRIMARY_MEDIUM_QUERY_ANCHOR_COUNT   = 3u;
static const uint PRIMARY_MEDIUM_QUERY_MAX_TRACE_HITS = 256u;

struct PrimaryMediumBoundaryHit
{
    uint   found;
    uint   instanceID;
    uint   materialID;
    float  rayT;
    float2 barycentrics;
    uint   primitiveID;
    uint   padding0;
};

void ResetPrimaryMediumSeed(uint status, out PrimaryRayMediumStackSeedData seed)
{
    seed.status   = status;
    seed.count    = 0u;
    seed.padding0 = 0u;
    seed.airCount = 0u;

    [unroll]
    for (uint i = 0u; i < kPrimaryRayMediumStackCapacity; ++i)
    {
        seed.entries[i].boundaryInstanceID = kInvalidIndex;
        seed.entries[i].mediumID            = kInvalidIndex;
        seed.entries[i].ior                 = 0.0;
        seed.entries[i].padding0            = 0u;
        seed.airEntries[i] = seed.entries[i];
    }
}

bool TryResolveQueryTriangle(
    SceneBufferIndices buffers,
    uint instanceID,
    uint primitiveID,
    float2 barycentrics,
    out float2 uv,
    out float3 geometricNormal)
{
    StructuredBuffer< MeshData >      meshes     = GetResource(buffers.meshes);
    StructuredBuffer< InstanceData >  instances  = GetResource(buffers.instances);
    StructuredBuffer< TransformData > transforms = GetResource(buffers.transforms);
    StructuredBuffer< uint >          indices    = GetResource(buffers.indices);
    StructuredBuffer< Vertex >        vertices   = GetResource(buffers.vertices);

    InstanceData  instance  = instances[instanceID];
    MeshData      mesh      = meshes[instance.meshID];
    TransformData transform = transforms[instance.transformID];

    uint indexBase = mesh.lods[0].iOffset + primitiveID * 3u;
    uint i0 = indices[indexBase + 0u];
    uint i1 = indices[indexBase + 1u];
    uint i2 = indices[indexBase + 2u];

    Vertex v0 = vertices[mesh.vOffset + i0];
    Vertex v1 = vertices[mesh.vOffset + i1];
    Vertex v2 = vertices[mesh.vOffset + i2];

    float3 bary = float3(
        1.0 - barycentrics.x - barycentrics.y,
        barycentrics.x,
        barycentrics.y);
    uv =
        v0.uv * bary.x +
        v1.uv * bary.y +
        v2.uv * bary.z;

    float3 edge01   = v1.position - v0.position;
    float3 edge02   = v2.position - v0.position;
    float3 normalOS = cross(edge01, edge02);

    float normalOSLength2 = dot(normalOS, normalOS);
    float edgeScale2      = dot(edge01, edge01) * dot(edge02, edge02);
    if (!IsPathFinite3(normalOS) || normalOSLength2 <= edgeScale2 * DEGENERATE_SIN2)
    {
        geometricNormal = float3(0.0);
        return false;
    }

    // Unit object-space normal through the inverse-transpose; only a singular transform gives zero length.
    float3x3 normalTransform = transpose((float3x3)transform.mWorldToLocal);
    float3 normalWS = mul(normalTransform, normalOS * rsqrt(normalOSLength2));
    float normalWSLength2 = dot(normalWS, normalWS);
    if (!IsPathFinite3(normalWS) || normalWSLength2 <= 0.0)
    {
        geometricNormal = float3(0.0);
        return false;
    }

    geometricNormal = normalWS * rsqrt(normalWSLength2);
    return IsPathFinite(uv.x) && IsPathFinite(uv.y);
}

bool HasQueryTransmissionLobe(StructuredBuffer< MaterialTextureData > materialTextures, MaterialData material, float2 uv)
{
    if ((material.materialFlags & (MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_ALPHA_BLEND | MATERIAL_FLAG_RELATIVE_IOR_INTERFACE)) != 0u)
        return false;

    if ((material.materialFlags & MATERIAL_FLAG_ALPHA_MASK) != 0u &&
        ReadOpacityLevel(materialTextures, material, uv) < material.alphaCutoff)
    {
        return false;
    }

    float metallic     = ReadMetallic(materialTextures, material, uv, float2(0.0), float2(0.0));
    float transmission = ReadTransmission(materialTextures, material, uv, float2(0.0), float2(0.0));
    return (1.0 - metallic) * transmission > PT_LOBE_EPS;
}

bool TryResolveQueryTerminalMedium(
    SceneBufferIndices buffers,
    StructuredBuffer< MaterialSlabData > materialSlabs,
    MaterialData rootMaterial,
    uint interiorMediumID,
    float2 uv,
    out Medium terminalMedium,
    out bool isTerminalMedium)
{
    StructuredBuffer< MaterialTextureData > materialTextures = GetResource(buffers.materialTextures);

    bool isMediumBoundary = (rootMaterial.materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u;
    isTerminalMedium = isMediumBoundary || HasQueryTransmissionLobe(materialTextures, rootMaterial, uv);
    terminalMedium = {};
    terminalMedium.mediumID = interiorMediumID;
    if (interiorMediumID == kInvalidIndex)
        return false;

    uint layerCount = max(rootMaterial.layerCount, 1u);
    if (isMediumBoundary && (layerCount > 1u ||
        (rootMaterial.materialFlags & (MATERIAL_FLAG_ALPHA_MASK | MATERIAL_FLAG_ALPHA_BLEND |
            MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_RELATIVE_IOR_INTERFACE)) != 0u))
        return false;

    if (layerCount > 1u)
    {
        if (rootMaterial.layerOffset == kInvalidIndex)
            return false;

        StructuredBuffer< MaterialData > materials = GetResource(buffers.materials);

        [loop]
        for (uint boundary = 1u; boundary < layerCount; ++boundary)
        {
            MaterialSlabData slab = materialSlabs[rootMaterial.layerOffset + boundary];
            if (slab.materialID == kInvalidIndex)
                return false;

            MaterialData boundaryMaterial = materials[slab.materialID];
            if ((boundaryMaterial.materialFlags & (MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_RELATIVE_IOR_INTERFACE)) != 0u)
                return false;

            isTerminalMedium = isTerminalMedium && HasQueryTransmissionLobe(materialTextures, boundaryMaterial, uv);
        }
    }

    MediumData opticalMedium;
    if (!TryLoadMedium(buffers, interiorMediumID, opticalMedium))
        return false;
    terminalMedium.ior = opticalMedium.ior;

    return IsPathFinite(terminalMedium.ior) &&
           terminalMedium.ior > 0.0;
}

bool FindNextMediumBoundary(SceneBufferIndices buffers, StructuredBuffer< MaterialSlabData > materialSlabs, RayDesc ray, out PrimaryMediumBoundaryHit hit)
{
    hit = {};

    RaytracingAccelerationStructure  tlas      = GetResource(buffers.tlas);
    StructuredBuffer< InstanceData > instances = GetResource(buffers.instances);
    StructuredBuffer< MaterialData > materials = GetResource(buffers.materials);

    RayQuery< RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES > query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
            continue;

        uint instanceID     = query.CandidateInstanceID();
        uint primitiveID    = query.CandidatePrimitiveIndex();
        float2 barycentrics = query.CandidateTriangleBarycentrics();

        uint materialID = instances[instanceID].materialID;
        if (materialID == kInvalidIndex)
            continue;

        MaterialData material = materials[materialID];
        float2 uv;
        float3 geometricNormal;
        bool validTriangle = TryResolveQueryTriangle(
            buffers,
            instanceID,
            primitiveID,
            barycentrics,
            uv,
            geometricNormal);
        if (!validTriangle)
        {
            // Commit malformed boundaries so replay reports failure instead of vacuum.
            if ((material.materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u ||
                ((material.materialFlags & (MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_ALPHA_BLEND | MATERIAL_FLAG_RELATIVE_IOR_INTERFACE)) == 0u &&
                    material.transmission > PT_LOBE_EPS))
            {
                query.CommitNonOpaqueTriangleHit();
            }
            continue;
        }

        Medium terminalMedium;
        bool isTerminalMedium;
        // Even an invalid flagged boundary must be replayed and rejected explicitly.
        if ((material.materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u ||
            (TryResolveQueryTerminalMedium(buffers, materialSlabs, material, instances[instanceID].mediumID, uv, terminalMedium, isTerminalMedium) && isTerminalMedium))
        {
            query.CommitNonOpaqueTriangleHit();
        }
    }

    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return false;

    hit.found        = 1u;
    hit.instanceID   = query.CommittedInstanceID();
    hit.primitiveID  = query.CommittedPrimitiveIndex();
    hit.materialID   = instances[hit.instanceID].materialID;
    hit.rayT         = query.CommittedRayT();
    hit.barycentrics = query.CommittedTriangleBarycentrics();
    return true;
}

uint ReplayMediumCrossings(
    SceneBufferIndices buffers,
    StructuredBuffer< MaterialSlabData > materialSlabs,
    float3 anchor,
    float3 cameraPosition,
    Medium exteriorMedium,
    uint maxTraceHits,
    out MediumStack stack,
    out MediumStack airStack)
{
    stack.Initialize(exteriorMedium);
    airStack.Initialize();

    float3 anchorToCamera = cameraPosition - anchor;
    float segmentLength2 = dot(anchorToCamera, anchorToCamera);
    if (!IsPathFinite(segmentLength2) || segmentLength2 <= sq(2.0 * PT_RAY_EPS))
        return PRIMARY_RAY_MEDIUM_SEED_STATUS_INVALID_PARAMS;

    float segmentLength = sqrt(segmentLength2);
    RayDesc ray;
    ray.Origin    = anchor;
    ray.Direction = anchorToCamera / segmentLength;
    ray.TMin      = PT_RAY_EPS;
    ray.TMax      = segmentLength - PT_RAY_EPS;

    StructuredBuffer< MaterialData > materials = GetResource(buffers.materials);
    StructuredBuffer< InstanceData > instances = GetResource(buffers.instances);

    [loop]
    for (uint hitIndex = 0u; hitIndex < maxTraceHits; ++hitIndex)
    {
        PrimaryMediumBoundaryHit hit;
        if (!FindNextMediumBoundary(buffers, materialSlabs, ray, hit))
            return PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID;

        float2 uv;
        float3 geometricNormal;
        if (!TryResolveQueryTriangle(buffers, hit.instanceID, hit.primitiveID, hit.barycentrics, uv, geometricNormal))
            return PRIMARY_RAY_MEDIUM_SEED_STATUS_TRACE_FAILED;

        MaterialData rootMaterial = materials[hit.materialID];

        Medium toMedium;
        bool isTerminalMedium;
        float NoW = dot(geometricNormal, -ray.Direction);
        if (!IsPathFinite(NoW) || abs(NoW) <= EPSILON_MIN ||
            !TryResolveQueryTerminalMedium(
                buffers,
                materialSlabs,
                rootMaterial,
                instances[hit.instanceID].mediumID,
                uv,
                toMedium,
                isTerminalMedium) || !isTerminalMedium)
        {
            return PRIMARY_RAY_MEDIUM_SEED_STATUS_TRACE_FAILED;
        }

        // No optical interface at an artificial enclosure; preserve the incident IOR.
        // Reference: https://www.pbrt.org/fileformat-v4#participating-media
        bool committed;
        if ((rootMaterial.materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u)
        {
            MediumData data;
            if (!TryLoadMedium(buffers, toMedium.mediumID, data))
                return PRIMARY_RAY_MEDIUM_SEED_STATUS_TRACE_FAILED;
            committed = TryCrossPureMediumBoundary(
                hit.instanceID, toMedium, data.mediumFlags, NoW, stack, airStack);
        }
        else
        {
            BoundaryMediumPair pair;
            if (!stack.TryResolveBoundaryMediums(hit.instanceID, toMedium, NoW, pair))
                return PRIMARY_RAY_MEDIUM_SEED_STATUS_TRACE_FAILED;

            if (pair.isEntering != 0u)
            {
                MediumEntry entry;
                entry.boundaryInstanceID = hit.instanceID;
                entry.medium             = pair.mediumT;
                committed = stack.TryPush(entry);
            }
            else
            {
                committed = stack.TryPop(hit.instanceID);
            }
        }

        if (!committed)
            return PRIMARY_RAY_MEDIUM_SEED_STATUS_TRACE_FAILED;

        ray.TMin = hit.rayT + max(PT_RAY_EPS, abs(hit.rayT) * 1.0e-6);
        if (ray.TMin >= ray.TMax)
            return PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID;
    }

    PrimaryMediumBoundaryHit overflowHit;
    return FindNextMediumBoundary(buffers, materialSlabs, ray, overflowHit)
        ? PRIMARY_RAY_MEDIUM_SEED_STATUS_HIT_LIMIT
        : PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID;
}

// Host order defines nesting. Air equality is membership equality; its stored IOR has no optical role.
bool MediumStacksEqual(MediumStack a, MediumStack b, bool bAirMembership)
{
    if (a.count != b.count)
        return false;

    for (uint i = 0u; i < a.count; ++i)
    {
        uint match = i;
        if (bAirMembership)
        {
            match = kInvalidIndex;
            for (uint j = 0u; j < b.count; ++j)
                if (a.entries[i].boundaryInstanceID == b.entries[j].boundaryInstanceID)
                {
                    match = j;
                    break;
                }
            if (match == kInvalidIndex)
                return false;
        }
        if (a.entries[i].boundaryInstanceID != b.entries[match].boundaryInstanceID ||
            a.entries[i].medium.mediumID != b.entries[match].medium.mediumID ||
            (!bAirMembership && asuint(a.entries[i].medium.ior) != asuint(b.entries[match].medium.ior)))
            return false;
    }
    return true;
}

void CopyMediumStack(MediumStack source, out MediumStack destination)
{
    destination.count = source.count;
    [unroll]
    for (uint i = 0u; i < kPrimaryRayMediumStackCapacity; ++i)
    {
        if (i < source.count)
        {
            destination.entries[i] = source.entries[i];
        }
        else
        {
            destination.entries[i].boundaryInstanceID = kInvalidIndex;
            destination.entries[i].medium.mediumID     = kInvalidIndex;
            destination.entries[i].medium.ior          = 0.0;
        }
    }
}

void CopyStackToSeed(MediumStack stack, MediumStack airStack, inout PrimaryRayMediumStackSeedData seed)
{
    seed.status = PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID;
    seed.count  = stack.count;
    seed.airCount = airStack.count;

    [unroll]
    for (uint i = 0u; i < kPrimaryRayMediumStackCapacity; ++i)
    {
        if (i < stack.count)
        {
            seed.entries[i].boundaryInstanceID = stack.entries[i].boundaryInstanceID;
            seed.entries[i].mediumID            = stack.entries[i].medium.mediumID;
            seed.entries[i].ior                 = stack.entries[i].medium.ior;
        }
        if (i < airStack.count)
        {
            seed.airEntries[i].boundaryInstanceID = airStack.entries[i].boundaryInstanceID;
            seed.airEntries[i].mediumID            = airStack.entries[i].medium.mediumID;
            seed.airEntries[i].ior                 = airStack.entries[i].medium.ior;
        }
    }
}

[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (any(dispatchThreadID != 0u))
        return;

    StructuredBuffer< SceneGlobals >                    sceneGlobals  = GetResource(g_Push.ID_Scene);
    StructuredBuffer< MaterialSlabData >                materialSlabs = GetResource(g_Push.ID_MaterialSlabs);
    RWStructuredBuffer< PrimaryRayMediumStackSeedData > seedOut       = GetResource(g_Push.ID_PrimaryRayMediumSeedOut);

    const SceneBufferIndices buffers        = sceneGlobals[0].buffers;
    const float3             cameraPosition = sceneGlobals[0].camera.position;

    PrimaryRayMediumStackSeedData seed;
    ResetPrimaryMediumSeed(PRIMARY_RAY_MEDIUM_SEED_STATUS_UNINITIALIZED, seed);

    float3 sceneBoundsCenter = g_Push.constants.sceneBoundsCenter;
    float sceneBoundsRadius = g_Push.constants.sceneBoundsRadius;
    float worldExteriorIOR  = g_Push.constants.worldExteriorIOR;
    uint maxTraceHits = min(g_Push.constants.maxTraceHits, PRIMARY_MEDIUM_QUERY_MAX_TRACE_HITS);

    if (!IsPathFinite3(sceneBoundsCenter) ||
        !IsPathFinite(sceneBoundsRadius) || sceneBoundsRadius <= PT_RAY_EPS ||
        !IsPathFinite3(cameraPosition) ||
        !IsPathFinite(worldExteriorIOR) || worldExteriorIOR <= 0.0 ||
        maxTraceHits == 0u)
    {
        ResetPrimaryMediumSeed(PRIMARY_RAY_MEDIUM_SEED_STATUS_INVALID_PARAMS, seed);
        seedOut[0] = seed;
        return;
    }

    static const float3 anchorDirections[PRIMARY_MEDIUM_QUERY_ANCHOR_COUNT] =
    {
        float3( 0.9205746,  0.3406126,  0.1933207),
        float3(-0.3713132,  0.9056420,  0.2082977),
        float3( 0.1519862, -0.4216088,  0.8939610)
    };
    float anchorDistance = sceneBoundsRadius * 1.25 + 16.0 * PT_RAY_EPS;

    Medium exteriorMedium;
    exteriorMedium.mediumID = g_Push.constants.worldExteriorMediumID;
    exteriorMedium.ior      = worldExteriorIOR;

    MediumStack referenceStack    = {};
    MediumStack referenceAirStack = {};
    // [unroll] leaves this early-exit loop rolled in Slang
    [ForceUnroll]
    for (uint anchorIndex = 0u; anchorIndex < PRIMARY_MEDIUM_QUERY_ANCHOR_COUNT; ++anchorIndex)
    {
        float3 anchor = sceneBoundsCenter + anchorDirections[anchorIndex] * anchorDistance;
        MediumStack candidateStack, candidateAirStack;
        uint traceStatus = ReplayMediumCrossings(
            buffers,
            materialSlabs,
            anchor,
            cameraPosition,
            exteriorMedium,
            maxTraceHits,
            candidateStack, candidateAirStack);
        if (traceStatus != PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID)
        {
            ResetPrimaryMediumSeed(traceStatus, seed);
            seedOut[0] = seed;
            return;
        }

        if (anchorIndex == 0u)
        {
            CopyMediumStack(candidateStack, referenceStack);
            CopyMediumStack(candidateAirStack, referenceAirStack);
        }
        else if (!MediumStacksEqual(referenceStack, candidateStack, false) ||
                 !MediumStacksEqual(referenceAirStack, candidateAirStack, true))
        {
            ResetPrimaryMediumSeed(PRIMARY_RAY_MEDIUM_SEED_STATUS_CONSENSUS_MISMATCH, seed);
            seedOut[0] = seed;
            return;
        }
    }

    if (referenceStack.count == 1u && exteriorMedium.mediumID != kInvalidIndex && referenceAirStack.count != 0u)
    {
        ResetPrimaryMediumSeed(PRIMARY_RAY_MEDIUM_SEED_STATUS_INVALID_PARAMS, seed);
        seedOut[0] = seed;
        return;
    }
    CopyStackToSeed(referenceStack, referenceAirStack, seed);
    seedOut[0] = seed;
}
