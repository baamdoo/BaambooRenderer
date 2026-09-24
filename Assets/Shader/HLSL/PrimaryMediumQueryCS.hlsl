#define _CAMERA
#define _MESH
#define _TRANSFORM
#define _MATERIAL
#define _PATH_TRACING
#include "Common.hlsli"

RaytracingAccelerationStructure g_Scene : register(t0, space1);
ConstantBuffer< PrimaryMediumQueryParams > g_PrimaryMediumQueryParams : register(b0, space1);
RWStructuredBuffer< PrimaryRayMediumStackSeedData > g_PrimaryRayMediumSeedOut : register(u0, space1);

ConstantBuffer< DescriptorHeapIndex > g_MaterialSlabs : register(b1, ROOT_CONSTANT_SPACE);

#include "PathUtils.hlsli"

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
    seed.padding1 = 0u;

    [unroll]
    for (uint i = 0u; i < PRIMARY_RAY_MEDIUM_STACK_CAPACITY; ++i)
    {
        seed.entries[i].boundaryInstanceID = INVALID_INDEX;
        seed.entries[i].mediumID            = INVALID_INDEX;
        seed.entries[i].ior                 = 0.0;
        seed.entries[i].padding0            = 0u;
    }
}

bool TryResolveQueryTriangle(
    uint instanceID,
    uint primitiveID,
    float2 barycentrics,
    out float2 uv,
    out float3 geometricNormal)
{
    StructuredBuffer< MeshData >      Meshes      = GetResource(g_Meshes.index);
    StructuredBuffer< InstanceData >  Instances   = GetResource(g_Instances.index);
    StructuredBuffer< TransformData > Transforms  = GetResource(g_Transforms.index);
    StructuredBuffer< uint >          IndexBuffer = GetResource(g_MeshStreams.indices);
    StructuredBuffer< Vertex >        VertexBuf   = GetResource(g_MeshStreams.vertices);

    InstanceData  instance  = Instances[instanceID];
    MeshData      mesh      = Meshes[instance.meshID];
    TransformData transform = Transforms[instance.transformID];

    uint indexBase = mesh.lods[0].iOffset + primitiveID * 3u;
    uint i0 = IndexBuffer[indexBase + 0u];
    uint i1 = IndexBuffer[indexBase + 1u];
    uint i2 = IndexBuffer[indexBase + 2u];

    Vertex v0 = VertexBuf[mesh.vOffset + i0];
    Vertex v1 = VertexBuf[mesh.vOffset + i1];
    Vertex v2 = VertexBuf[mesh.vOffset + i2];

    float3 bary = float3(
        1.0 - barycentrics.x - barycentrics.y,
        barycentrics.x,
        barycentrics.y);
    uv =
        float2(v0.u, v0.v) * bary.x +
        float2(v1.u, v1.v) * bary.y +
        float2(v2.u, v2.v) * bary.z;

    float3 p0 = float3(v0.posX, v0.posY, v0.posZ);
    float3 p1 = float3(v1.posX, v1.posY, v1.posZ);
    float3 p2 = float3(v2.posX, v2.posY, v2.posZ);
    
    float3 edge01   = p1 - p0;
    float3 edge02   = p2 - p0;
    float3 normalOS = cross(edge01, edge02);

    float normalOSLength2 = dot(normalOS, normalOS);
    float edgeScale2      = dot(edge01, edge01) * dot(edge02, edge02);
    if (!IsPathFinite3(normalOS) || normalOSLength2 <= edgeScale2 * DEGENERATE_SIN2)
    {
        geometricNormal = 0.0;
        return false;
    }

    // Unit object-space normal through the inverse-transpose; only a singular transform gives zero length.
    float3x3 normalTransform = transpose((float3x3)transform.mWorldToLocal);
    float3 normalWS = mul(normalTransform, normalOS * rsqrt(normalOSLength2));
    float normalWSLength2 = dot(normalWS, normalWS);
    if (!IsPathFinite3(normalWS) || normalWSLength2 <= 0.0)
    {
        geometricNormal = 0.0;
        return false;
    }

    geometricNormal = normalWS * rsqrt(normalWSLength2);
    return IsPathFinite(uv.x) && IsPathFinite(uv.y);
}

bool HasQueryTransmissionLobe(MaterialData material, float2 uv)
{
    if ((material.materialFlags & (MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_ALPHA_BLEND | MATERIAL_FLAG_RELATIVE_IOR_INTERFACE)) != 0u)
        return false;

    if ((material.materialFlags & MATERIAL_FLAG_ALPHA_MASK) != 0u &&
        ReadOpacityLevel(material, uv) < material.alphaCutoff)
    {
        return false;
    }

    float metallic     = ReadMetallic(material, uv, 0.0, 0.0);
    float transmission = ReadTransmission(material, uv, 0.0, 0.0);
    return (1.0 - metallic) * transmission > PT_LOBE_EPS;
}

bool TryResolveQueryTerminalMedium(
    MaterialData rootMaterial,
    uint rootMaterialID,
    float2 uv,
    out Medium terminalMedium,
    out bool isTerminalMedium)
{
    isTerminalMedium = HasQueryTransmissionLobe(rootMaterial, uv);

    uint layerCount = max(rootMaterial.layerCount, 1u);
    if (layerCount == 1u)
    {
        terminalMedium.mediumID = rootMaterialID;
        terminalMedium.ior      = max(rootMaterial.ior, 1.0e-4);
    }
    else
    {
        if (rootMaterial.layerOffset == INVALID_INDEX)
            return false;

        StructuredBuffer< MaterialSlabData > Slabs = GetResource(g_MaterialSlabs.index);
        StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);

        [loop]
        for (uint boundary = 1u; boundary < layerCount; ++boundary)
        {
            MaterialSlabData slab = Slabs[rootMaterial.layerOffset + boundary];
            if (slab.materialID == INVALID_INDEX)
                return false;

            MaterialData boundaryMaterial = Materials[slab.materialID];
            if ((boundaryMaterial.materialFlags & (MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_RELATIVE_IOR_INTERFACE)) != 0u)
                return false;

            isTerminalMedium = isTerminalMedium && HasQueryTransmissionLobe(boundaryMaterial, uv);
            if (boundary == layerCount - 1u)
            {
                terminalMedium.mediumID = slab.materialID;
                terminalMedium.ior      = max(boundaryMaterial.ior, 1.0e-4);
            }
        }
    }

    return terminalMedium.mediumID != INVALID_INDEX &&
           IsPathFinite(terminalMedium.ior) &&
           terminalMedium.ior > 0.0;
}

bool FindNextMediumBoundary(RayDesc ray, out PrimaryMediumBoundaryHit hit)
{
    hit = (PrimaryMediumBoundaryHit)0;

    RayQuery< RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES > query;
    query.TraceRayInline(g_Scene, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
            continue;

        uint instanceID     = query.CandidateInstanceID();
        uint primitiveID    = query.CandidatePrimitiveIndex();
        float2 barycentrics = query.CandidateTriangleBarycentrics();

        StructuredBuffer< InstanceData > Instances = GetResource(g_Instances.index);
        StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);
        uint materialID = Instances[instanceID].materialID;
        if (materialID == INVALID_INDEX)
            continue;

        MaterialData material = Materials[materialID];
        float2 uv;
        float3 geometricNormal;
        bool validTriangle = TryResolveQueryTriangle(
            instanceID,
            primitiveID,
            barycentrics,
            uv,
            geometricNormal);
        if (!validTriangle)
        {
            // Commit malformed, potentially transmissive solids so replay fails closed.
            if ((material.materialFlags & (MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_ALPHA_BLEND | MATERIAL_FLAG_RELATIVE_IOR_INTERFACE)) == 0u &&
                material.transmission > PT_LOBE_EPS)
            {
                query.CommitNonOpaqueTriangleHit();
            }
            continue;
        }

        Medium terminalMedium;
        bool isTerminalMedium;
        if (TryResolveQueryTerminalMedium(material, materialID, uv, terminalMedium, isTerminalMedium) && isTerminalMedium)
        {
            query.CommitNonOpaqueTriangleHit();
        }
    }

    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return false;

    StructuredBuffer< InstanceData > Instances = GetResource(g_Instances.index);
    hit.found        = 1u;
    hit.instanceID   = query.CommittedInstanceID();
    hit.primitiveID  = query.CommittedPrimitiveIndex();
    hit.materialID   = Instances[hit.instanceID].materialID;
    hit.rayT         = query.CommittedRayT();
    hit.barycentrics = query.CommittedTriangleBarycentrics();
    return true;
}

uint ReplayMediumCrossings(float3 anchor, float3 cameraPosition, Medium exteriorMedium, uint maxTraceHits, out MediumStack stack)
{
    stack.Initialize(exteriorMedium);

    float3 anchorToCamera = cameraPosition - anchor;
    float segmentLength2 = dot(anchorToCamera, anchorToCamera);
    if (!IsPathFinite3(anchorToCamera) || !IsPathFinite(segmentLength2) || segmentLength2 <= sq(2.0 * PT_RAY_EPS))
        return PRIMARY_RAY_MEDIUM_SEED_STATUS_INVALID_PARAMS;

    float segmentLength = sqrt(segmentLength2);
    RayDesc ray;
    ray.Origin    = anchor;
    ray.Direction = anchorToCamera / segmentLength;
    ray.TMin      = PT_RAY_EPS;
    ray.TMax      = segmentLength - PT_RAY_EPS;

    [loop]
    for (uint hitIndex = 0u; hitIndex < maxTraceHits; ++hitIndex)
    {
        PrimaryMediumBoundaryHit hit;
        if (!FindNextMediumBoundary(ray, hit))
            return PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID;

        float2 uv;
        float3 geometricNormal;
        if (!TryResolveQueryTriangle(hit.instanceID, hit.primitiveID, hit.barycentrics, uv, geometricNormal))
            return PRIMARY_RAY_MEDIUM_SEED_STATUS_TRACE_FAILED;

        StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);
        MaterialData rootMaterial = Materials[hit.materialID];

        Medium toMedium;
        bool isTerminalMedium;
        float NoW = dot(geometricNormal, -ray.Direction);
        BoundaryMediumPair pair;
        if (!IsPathFinite(NoW) || abs(NoW) <= EPSILON_MIN ||
            !TryResolveQueryTerminalMedium(
                rootMaterial,
                hit.materialID,
                uv,
                toMedium,
                isTerminalMedium) || !isTerminalMedium ||
            !stack.TryResolveBoundaryMediums(hit.instanceID, toMedium, NoW, pair))
        {
            return PRIMARY_RAY_MEDIUM_SEED_STATUS_TRACE_FAILED;
        }

        bool committed;
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

        if (!committed)
            return PRIMARY_RAY_MEDIUM_SEED_STATUS_TRACE_FAILED;

        ray.TMin = hit.rayT + max(PT_RAY_EPS, abs(hit.rayT) * 1.0e-6);
        if (ray.TMin >= ray.TMax)
            return PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID;
    }

    PrimaryMediumBoundaryHit overflowHit;
    return FindNextMediumBoundary(ray, overflowHit)
        ? PRIMARY_RAY_MEDIUM_SEED_STATUS_HIT_LIMIT
        : PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID;
}

bool MediumStacksEqual(MediumStack a, MediumStack b)
{
    if (a.count != b.count)
        return false;

    [unroll]
    for (uint i = 0u; i < PRIMARY_RAY_MEDIUM_STACK_CAPACITY; ++i)
    {
        if (i >= a.count)
            break;

        if (a.entries[i].boundaryInstanceID != b.entries[i].boundaryInstanceID ||
            a.entries[i].medium.mediumID != b.entries[i].medium.mediumID ||
            asuint(a.entries[i].medium.ior) != asuint(b.entries[i].medium.ior))
        {
            return false;
        }
    }
    return true;
}

void CopyMediumStack(MediumStack source, out MediumStack destination)
{
    destination.count = source.count;
    [unroll]
    for (uint i = 0u; i < PRIMARY_RAY_MEDIUM_STACK_CAPACITY; ++i)
    {
        if (i < source.count)
        {
            destination.entries[i] = source.entries[i];
        }
        else
        {
            destination.entries[i].boundaryInstanceID = INVALID_INDEX;
            destination.entries[i].medium.mediumID     = INVALID_INDEX;
            destination.entries[i].medium.ior          = 0.0;
        }
    }
}

void CopyStackToSeed(MediumStack stack, inout PrimaryRayMediumStackSeedData seed)
{
    seed.status = PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID;
    seed.count  = stack.count;

    [unroll]
    for (uint i = 0u; i < PRIMARY_RAY_MEDIUM_STACK_CAPACITY; ++i)
    {
        if (i >= stack.count)
            break;

        seed.entries[i].boundaryInstanceID = stack.entries[i].boundaryInstanceID;
        seed.entries[i].mediumID            = stack.entries[i].medium.mediumID;
        seed.entries[i].ior                 = stack.entries[i].medium.ior;
    }
}

[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (any(dispatchThreadID != 0u))
        return;

    PrimaryRayMediumStackSeedData seed;
    ResetPrimaryMediumSeed(PRIMARY_RAY_MEDIUM_SEED_STATUS_UNINITIALIZED, seed);

    float3 sceneBoundsCenter = float3(
        g_PrimaryMediumQueryParams.sceneBoundsCenterX,
        g_PrimaryMediumQueryParams.sceneBoundsCenterY,
        g_PrimaryMediumQueryParams.sceneBoundsCenterZ);
    float sceneBoundsRadius = g_PrimaryMediumQueryParams.sceneBoundsRadius;
    float worldExteriorIOR  = g_PrimaryMediumQueryParams.worldExteriorIOR;
    uint maxTraceHits = min(g_PrimaryMediumQueryParams.maxTraceHits, PRIMARY_MEDIUM_QUERY_MAX_TRACE_HITS);

    if (!IsPathFinite3(sceneBoundsCenter) ||
        !IsPathFinite(sceneBoundsRadius) || sceneBoundsRadius <= PT_RAY_EPS ||
        !IsPathFinite3(g_Camera.posWORLD) ||
        !IsPathFinite(worldExteriorIOR) || worldExteriorIOR <= 0.0 ||
        maxTraceHits == 0u)
    {
        ResetPrimaryMediumSeed(PRIMARY_RAY_MEDIUM_SEED_STATUS_INVALID_PARAMS, seed);
        g_PrimaryRayMediumSeedOut[0] = seed;
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
    exteriorMedium.mediumID = g_PrimaryMediumQueryParams.worldExteriorMediumID;
    exteriorMedium.ior      = worldExteriorIOR;

    MediumStack referenceStack;
    [unroll]
    for (uint anchorIndex = 0u; anchorIndex < PRIMARY_MEDIUM_QUERY_ANCHOR_COUNT; ++anchorIndex)
    {
        float3 anchor = sceneBoundsCenter + anchorDirections[anchorIndex] * anchorDistance;
        MediumStack candidateStack;
        uint traceStatus = ReplayMediumCrossings(
            anchor,
            g_Camera.posWORLD,
            exteriorMedium,
            maxTraceHits,
            candidateStack);
        if (traceStatus != PRIMARY_RAY_MEDIUM_SEED_STATUS_VALID)
        {
            ResetPrimaryMediumSeed(traceStatus, seed);
            g_PrimaryRayMediumSeedOut[0] = seed;
            return;
        }

        if (anchorIndex == 0u)
        {
            CopyMediumStack(candidateStack, referenceStack);
        }
        else if (!MediumStacksEqual(referenceStack, candidateStack))
        {
            ResetPrimaryMediumSeed(PRIMARY_RAY_MEDIUM_SEED_STATUS_CONSENSUS_MISMATCH, seed);
            g_PrimaryRayMediumSeedOut[0] = seed;
            return;
        }
    }

    CopyStackToSeed(referenceStack, seed);
    g_PrimaryRayMediumSeedOut[0] = seed;
}
