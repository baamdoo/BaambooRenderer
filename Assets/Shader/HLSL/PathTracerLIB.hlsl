#define _CAMERA
#define _MESH
#define _MATERIAL
#define _LIGHT
#define _PATH_TRACING
#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_AccumulatedSampleCount;
    uint g_SamplesPerFrame;
    uint g_ResetAccumulation;
    uint g_MaxDepth;

    float3 g_EnvironmentRadiance;
    uint   g_UseEnvironmentMap;
    uint   g_UseEnvironmentSampling;
    uint   g_EnvironmentDistributionWidth;
    uint   g_EnvironmentDistributionHeight;
    uint   g_EnvironmentPadding0;
};

RaytracingAccelerationStructure g_Scene : register(t0, space1);

ConstantBuffer< DescriptorHeapIndex > g_Accumulation : register(b1,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Radiance     : register(b2,  ROOT_CONSTANT_SPACE);
#if PT_VALIDATION
ConstantBuffer< DescriptorHeapIndex > g_Albedo          : register(b3,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Normal          : register(b4,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Depth           : register(b5,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_GeometricNormal : register(b6,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MaterialParams        : register(b7,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MaterialExtra         : register(b8,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MaterialSpecularColor : register(b9,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Emission             : register(b10, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_DiffuseRadiance      : register(b11, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SpecularRadiance     : register(b12, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TransmissionRadiance : register(b13, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SurfaceLobeMask      : register(b14, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SurfaceLobeWeight    : register(b15, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SampledLobeFrequency : register(b16, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_PrimaryId              : register(b17, ROOT_CONSTANT_SPACE);
#endif // PT_VALIDATION
ConstantBuffer< DescriptorHeapIndex > g_EnvironmentMap          : register(b18, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_EnvironmentDistribution : register(b19, ROOT_CONSTANT_SPACE);

#include "PathSurface.hlsli"
#include "PathComposite.hlsli"
#include "PathValidation.hlsli"
#include "PathSampling.hlsli"

void OrientOpaqueSurfaceNormalForPath(inout SurfacePayload hp, float3 woWS)
{
    if (hp.transmission > PT_LOBE_EPS)
        return;

    if (dot(hp.normal, woWS) < 0.0)
        hp.normal = -hp.normal;
}

float3 TracePath(RayDesc primaryRay, uint2 pixel, uint sampleIndex
#if PT_VALIDATION
    , out SurfacePayload primaryHit
    , out PathContribution contribution
    , out PathBSDFSample primaryBSDFSample
#endif
)
{
    RngState rng = InitRng(pixel, 0u, sampleIndex);

    RayDesc ray = primaryRay;

    float3 L    = float3(0.0, 0.0, 0.0); // radiance carried back along this path
    float3 beta = float3(1.0, 1.0, 1.0); // β — path throughput: running product of f·cosθ/pdf

#if PT_VALIDATION
    primaryHit        = (SurfacePayload)0;
    contribution      = ZeroPathContribution();
    primaryBSDFSample = (PathBSDFSample)0;
#endif

    uint  prevBSDFFlags = 0u;
    float prevPdfBSDF   = 0.0;
    uint  wasDelta      = 0u;

    uint maxDepth = clamp(g_MaxDepth, 1u, PT_MAX_DEPTH_LIMIT);
    [loop]
    for (uint depth = 0u; depth < maxDepth; ++depth)
    {
        SurfacePayload hp = (SurfacePayload)0;

        TraceRay(g_Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, hp);
        if (hp.hitKind != 0u)
            OrientOpaqueSurfaceNormalForPath(hp, -ray.Direction);
#if PT_VALIDATION
        if (depth == 0u)
            primaryHit = hp;
#endif
        // ── Case B: miss — the path escapes to the environment ─────────
        if (hp.hitKind == 0u)
        {
            float misWeight = (depth > 0u)
                ? CalculateMISWeight(prevPdfBSDF, EnvironmentPDF(ray.Direction), wasDelta)
                : 1.0; // depth 0: camera ray looked straight at the sky

            float3 environmentContribution = beta * EvaluateEnvironmentRadiance(ray.Direction) * misWeight;
            if (IsPathFinite3(environmentContribution))
            {
                L += environmentContribution;
#if PT_VALIDATION
                AccumulatePathContribution(contribution, prevBSDFFlags, environmentContribution);
#endif
            }
            break;
        }

        // ── Case A: the random walk hit an emitter ─────────────────────
        if (any(hp.emission > 0.0))
        {
            float3 emittedContribution = beta * hp.emission;
            if (depth == 0u)
            {
                if (IsPathFinite3(emittedContribution))
                    L += emittedContribution;
            }
            else
            {
                float pdfLight  = AreaLightPDFAtHit(ray.Origin, hp.position, ray.Direction);
                float misWeight = CalculateMISWeight(prevPdfBSDF, pdfLight, wasDelta);
                emittedContribution *= misWeight;

                if (IsPathFinite3(emittedContribution))
                {
                    L += emittedContribution;
#if PT_VALIDATION
                    AccumulatePathContribution(contribution, prevBSDFFlags, emittedContribution);
#endif
                }
            }
            break;
        }

        // ── Case C: surface hit — NEE, then extend the walk ────────────
        BxDF::Frame frame = MakeSurfaceFrame(hp);

        SurfaceMaterial material = MakeSurfaceMaterial(hp);
        float3 woWS = -ray.Direction;
#if PT_VALIDATION
        PathContribution directContribution;
        float3 directLighting = EstimateDirectLighting(hp.position, hp.normal, frame, woWS, material, rng, directContribution);

        PathContribution environmentDirectContribution;
        float3 environmentDirectLighting = EstimateEnvironmentDirectLighting(hp.position, hp.normal, frame, woWS, material, (depth + 1u) < maxDepth, rng, environmentDirectContribution);
#else
        float3 directLighting = EstimateDirectLighting(hp.position, hp.normal, frame, woWS, material, rng);
        float3 environmentDirectLighting = EstimateEnvironmentDirectLighting(hp.position, hp.normal, frame, woWS, material, (depth + 1u) < maxDepth, rng);
#endif
        float3 directContributionSum = beta * (directLighting + environmentDirectLighting);
        if (IsPathFinite3(directContributionSum))
            L += directContributionSum;
#if PT_VALIDATION
        contribution.diffuse      += beta * (directContribution.diffuse + environmentDirectContribution.diffuse);
        contribution.specular     += beta * (directContribution.specular + environmentDirectContribution.specular);
        contribution.transmission += beta * (directContribution.transmission + environmentDirectContribution.transmission);
#endif

        float3 wo = BxDF::ToLocal(frame, woWS);
        PathBSDFSample bsdfSample = BxDF::Composite::SampleRay(material, wo, rng);
#if PT_VALIDATION
        if (depth == 0u)
            primaryBSDFSample = bsdfSample;
#endif
        if (bsdfSample.valid == 0u)
            break;

        if (!IsPathFinite3(bsdfSample.wi) || !IsPathFinite3(bsdfSample.weight) || !IsPathFinite(bsdfSample.pdf))
            break;
        if (bsdfSample.isDelta == 0u && bsdfSample.pdf <= 0.0)
            break;

        // ===== β update ===============================
        beta *= bsdfSample.weight;
        if (!IsPathFinite3(beta))
            break;

        prevBSDFFlags = bsdfSample.flags;
        prevPdfBSDF   = bsdfSample.pdf;
        wasDelta      = bsdfSample.isDelta;

        // ===== Russian Roulette ========================================
        const float rrThreshold = 0.05;
        if (depth >= 3)
        {
            float qSurvive = clamp(max(beta.r, max(beta.g, beta.b)), rrThreshold, 1.0 - rrThreshold);
            if (NextFloat(rng) >= qSurvive)
                break;

            beta /= qSurvive;
        }

        float3 wiWS = normalize(BxDF::ToWorld(frame, bsdfSample.wi));
        if (!IsPathFinite3(wiWS))
            break;

        ray.Origin    = OffsetRay(hp.position, hp.geometricNormal, wiWS);
        ray.Direction = wiWS;
        ray.TMin      = PT_RAY_EPS;
        ray.TMax      = g_Camera.zFar;
    }

    return L;
}

[shader("raygeneration")]
void RayGen()
{
    RWTexture2D< float4 > Accumulation = GetResource(g_Accumulation.index);
    RWTexture2D< float4 > Radiance     = GetResource(g_Radiance.index);

#if PT_VALIDATION
    RWTexture2D< float4 > Albedo          = GetResource(g_Albedo.index);
    RWTexture2D< float4 > Normal          = GetResource(g_Normal.index);
    RWTexture2D< float4 > Depth           = GetResource(g_Depth.index);
    RWTexture2D< float4 > GeometricNormal = GetResource(g_GeometricNormal.index);

    RWTexture2D< float4 > MaterialParams        = GetResource(g_MaterialParams.index);
    RWTexture2D< float4 > MaterialExtra         = GetResource(g_MaterialExtra.index);
    RWTexture2D< float4 > MaterialSpecularColor = GetResource(g_MaterialSpecularColor.index);

    RWTexture2D< float4 > Emission             = GetResource(g_Emission.index);
    RWTexture2D< float4 > DiffuseRadiance      = GetResource(g_DiffuseRadiance.index);
    RWTexture2D< float4 > SpecularRadiance     = GetResource(g_SpecularRadiance.index);
    RWTexture2D< float4 > TransmissionRadiance = GetResource(g_TransmissionRadiance.index);

    RWTexture2D< float4 > SurfaceLobeMaskAOV      = GetResource(g_SurfaceLobeMask.index);
    RWTexture2D< float4 > SurfaceLobeWeightAOV    = GetResource(g_SurfaceLobeWeight.index);
    RWTexture2D< float4 > SampledLobeFrequencyAOV = GetResource(g_SampledLobeFrequency.index);
    RWTexture2D< float4 > PrimaryIdAOV             = GetResource(g_PrimaryId.index);
#endif

    RayDesc ray;
    uint2 rayIndex      = DispatchRaysIndex().xy;
    uint2 rayDimensions = DispatchRaysDimensions().xy;

    uint   numSamples = max(g_SamplesPerFrame, 1u);
    float3 Lsum       = float3(0.0, 0.0, 0.0); // sum of this frame's path samples

#if PT_VALIDATION
    PathValidationSums validationSums = ZeroPathValidationSums();
#endif

    [loop]
    for (uint sampleOffset = 0u; sampleOffset < numSamples; ++sampleOffset)
    {
        uint sampleIndex = g_AccumulatedSampleCount + sampleOffset;
        RngState rng = InitRng(rayIndex, 0u, sampleIndex);

        float2 pixelJitter = SamplePixelTent(NextFloat2(rng));
        float2 pathUV      = (float2(rayIndex) + 0.5 + pixelJitter) / float2(rayDimensions);
        float3 pathTarget  = ReconstructWorldPos(pathUV, 1.0, g_Camera.mViewProjInv);

        ray.Origin    = g_Camera.posWORLD;
        ray.Direction = normalize(pathTarget - g_Camera.posWORLD);
        ray.TMin      = g_Camera.zNear;
        ray.TMax      = g_Camera.zFar;

#if PT_VALIDATION
        SurfacePayload primaryHit = (SurfacePayload)0;

        PathContribution pathContribution;
        PathBSDFSample primaryBSDFSample = (PathBSDFSample)0;

        float3 sampleRadiance = TracePath(ray, rayIndex, sampleIndex, primaryHit, pathContribution, primaryBSDFSample);
        if (IsPathFinite3(sampleRadiance))
            Lsum += sampleRadiance;

        AccumulateValidationContribution(validationSums, pathContribution);

        if (primaryHit.hitKind != 0u)
            validationSums.sampledLobeFrequency += BxDF::Composite::SampledLobeVector(primaryBSDFSample);
#else
        float3 sampleRadiance = TracePath(ray, rayIndex, sampleIndex);
        if (IsPathFinite3(sampleRadiance))
            Lsum += sampleRadiance;
#endif
    }

#if PT_VALIDATION
    PathValidationSums primaryValidation = ZeroPathValidationSums();
    {
        RayDesc primaryValidationRay;
        float2 primaryValidationUV = (float2(rayIndex) + 0.5) / float2(rayDimensions);
        float3 primaryValidationTarget = ReconstructWorldPos(primaryValidationUV, 1.0, g_Camera.mViewProjInv);

        primaryValidationRay.Origin    = g_Camera.posWORLD;
        primaryValidationRay.Direction = normalize(primaryValidationTarget - g_Camera.posWORLD);
        primaryValidationRay.TMin      = g_Camera.zNear;
        primaryValidationRay.TMax      = g_Camera.zFar;

        SurfacePayload primaryValidationHit = (SurfacePayload)0;
        PathContribution unusedPrimaryContribution;
        PathBSDFSample primaryValidationBSDFSample = (PathBSDFSample)0;
        TracePath(primaryValidationRay, rayIndex, 0u, primaryValidationHit, unusedPrimaryContribution, primaryValidationBSDFSample);

        if (primaryValidationHit.hitKind == 0u)
            AccumulatePrimaryMissValidation(primaryValidation);
        else
            AccumulatePrimaryHitValidation(primaryValidation, primaryValidationHit, primaryValidationRay, primaryValidationBSDFSample);
    }
#endif

    bool bReset = (g_ResetAccumulation != 0u) || (g_AccumulatedSampleCount == 0u);

    // Accumulation
    float4 previousAccumulation  = bReset ? float4(0.0, 0.0, 0.0, 0.0) : Accumulation[rayIndex];
    if (!IsPathFinite3(previousAccumulation.rgb) || !IsPathFinite(previousAccumulation.a))
        previousAccumulation = float4(0.0, 0.0, 0.0, 0.0);
    if (!IsPathFinite3(Lsum))
        Lsum = float3(0.0, 0.0, 0.0);
    
    float3 accumulatedRadiance   = previousAccumulation.rgb + Lsum;
    float  accumulatedSamples    = previousAccumulation.a + float(numSamples);
    float  invAccumulatedSamples = 1.0 / accumulatedSamples;

    Accumulation[rayIndex] = float4(accumulatedRadiance, accumulatedSamples);
    Radiance[rayIndex]     = float4(accumulatedRadiance * invAccumulatedSamples, 1.0);

#if PT_VALIDATION
    float previousSamples = previousAccumulation.a;

    PathValidationSums accumulatedValidation = ZeroPathValidationSums();
    accumulatedValidation.albedo          = AccumulatedValidationAverage(bReset, Albedo[rayIndex].rgb, previousSamples, validationSums.albedo);
    accumulatedValidation.normal          = AccumulatedValidationAverage(bReset, Normal[rayIndex].rgb, previousSamples, validationSums.normal);
    accumulatedValidation.depth           = AccumulatedValidationAverage(bReset, Depth[rayIndex].rgb, previousSamples, validationSums.depth);
    accumulatedValidation.geometricNormal = AccumulatedValidationAverage(bReset, GeometricNormal[rayIndex].rgb, previousSamples, validationSums.geometricNormal);

    accumulatedValidation.materialParams        = AccumulatedValidationAverage(bReset, MaterialParams[rayIndex].rgb, previousSamples, validationSums.materialParams);
    accumulatedValidation.materialExtra         = AccumulatedValidationAverage(bReset, MaterialExtra[rayIndex].rgb, previousSamples, validationSums.materialExtra);
    accumulatedValidation.materialSpecularColor = AccumulatedValidationAverage(bReset, MaterialSpecularColor[rayIndex].rgb, previousSamples, validationSums.materialSpecularColor);

    accumulatedValidation.emission             = AccumulatedValidationAverage(bReset, Emission[rayIndex].rgb, previousSamples, validationSums.emission);
    accumulatedValidation.diffuseRadiance      = AccumulatedValidationAverage(bReset, DiffuseRadiance[rayIndex].rgb, previousSamples, validationSums.diffuseRadiance);
    accumulatedValidation.specularRadiance     = AccumulatedValidationAverage(bReset, SpecularRadiance[rayIndex].rgb, previousSamples, validationSums.specularRadiance);
    accumulatedValidation.transmissionRadiance = AccumulatedValidationAverage(bReset, TransmissionRadiance[rayIndex].rgb, previousSamples, validationSums.transmissionRadiance);

    accumulatedValidation.surfaceLobeMask      = AccumulatedValidationAverage(bReset, SurfaceLobeMaskAOV[rayIndex].rgb, previousSamples, validationSums.surfaceLobeMask);
    accumulatedValidation.surfaceLobeWeight    = AccumulatedValidationAverage(bReset, SurfaceLobeWeightAOV[rayIndex].rgb, previousSamples, validationSums.surfaceLobeWeight);
    accumulatedValidation.sampledLobeFrequency = AccumulatedValidationAverage(bReset, SampledLobeFrequencyAOV[rayIndex].rgb, previousSamples, validationSums.sampledLobeFrequency);
    accumulatedValidation.primaryId            = AccumulatedValidationAverage(bReset, PrimaryIdAOV[rayIndex].rgb, previousSamples, validationSums.primaryId);

    Albedo[rayIndex]          = float4(primaryValidation.albedo, 1.0);
    Normal[rayIndex]          = float4(primaryValidation.normal, 1.0);
    Depth[rayIndex]           = float4(primaryValidation.depth, 1.0);
    GeometricNormal[rayIndex] = float4(primaryValidation.geometricNormal, 1.0);

    MaterialParams[rayIndex]        = float4(primaryValidation.materialParams, 1.0);
    MaterialExtra[rayIndex]         = float4(primaryValidation.materialExtra, 1.0);
    MaterialSpecularColor[rayIndex] = float4(primaryValidation.materialSpecularColor, 1.0);

    Emission[rayIndex]             = float4(primaryValidation.emission, 1.0);
    DiffuseRadiance[rayIndex]      = float4(accumulatedValidation.diffuseRadiance * invAccumulatedSamples, 1.0);
    SpecularRadiance[rayIndex]     = float4(accumulatedValidation.specularRadiance * invAccumulatedSamples, 1.0);
    TransmissionRadiance[rayIndex] = float4(accumulatedValidation.transmissionRadiance * invAccumulatedSamples, 1.0);

    SurfaceLobeMaskAOV[rayIndex]      = float4(primaryValidation.surfaceLobeMask, 1.0);
    SurfaceLobeWeightAOV[rayIndex]    = float4(primaryValidation.surfaceLobeWeight, 1.0);
    SampledLobeFrequencyAOV[rayIndex] = float4(accumulatedValidation.sampledLobeFrequency * invAccumulatedSamples, 1.0);
    PrimaryIdAOV[rayIndex]             = float4(primaryValidation.primaryId, 1.0);
#endif
}

[shader("miss")]
void PrimaryMiss(inout SurfacePayload hp)
{
    hp.hitKind = 0u;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload sp)
{
    sp.visible = 1u;
}

bool AlphaTestRejectsHit(in BuiltInTriangleIntersectionAttributes attr)
{
    StructuredBuffer< MeshData >     Meshes      = GetResource(g_Meshes.index);
    StructuredBuffer< InstanceData > Instances   = GetResource(g_Instances.index);
    StructuredBuffer< uint >         IndexBuffer = GetResource(g_MeshStreams.indices);
    StructuredBuffer< Vertex >       VertexBuf   = GetResource(g_MeshStreams.vertices);
    StructuredBuffer< MaterialData > Materials   = GetResource(g_Materials.index);

    InstanceData instance = Instances[InstanceID()];
    if (instance.materialID == INVALID_INDEX)
        return false;

    MaterialData mat = Materials[instance.materialID];
    if (mat.alphaCutoff <= 0.0 || mat.albedoID == INVALID_INDEX)
        return false;

    MeshData mesh = Meshes[instance.meshID];
    uint primIdx = PrimitiveIndex();
    uint indexBase = mesh.lods[0].iOffset + primIdx * 3u;
    uint i0 = IndexBuffer[indexBase + 0u];
    uint i1 = IndexBuffer[indexBase + 1u];
    uint i2 = IndexBuffer[indexBase + 2u];

    Vertex v0 = VertexBuf[mesh.vOffset + i0];
    Vertex v1 = VertexBuf[mesh.vOffset + i1];
    Vertex v2 = VertexBuf[mesh.vOffset + i2];

    float3 bary = float3(
        1.0 - attr.barycentrics.x - attr.barycentrics.y,
        attr.barycentrics.x,
        attr.barycentrics.y);

    float2 uv =
        float2(v0.u, v0.v) * bary.x +
        float2(v1.u, v1.v) * bary.y +
        float2(v2.u, v2.v) * bary.z;

    return ReadOpacity(mat, uv) < mat.alphaCutoff;
}

[shader("anyhit")]
void AnyHit_PrimaryAlpha(inout SurfacePayload hp, in BuiltInTriangleIntersectionAttributes attr)
{
    if (AlphaTestRejectsHit(attr))
        IgnoreHit();
}

[shader("anyhit")]
void AnyHit_ShadowAlpha(inout ShadowPayload sp, in BuiltInTriangleIntersectionAttributes attr)
{
    if (AlphaTestRejectsHit(attr))
        IgnoreHit();
}

[shader("closesthit")]
void ClosestHit_Primary(inout SurfacePayload hp, in BuiltInTriangleIntersectionAttributes attr)
{
    StructuredBuffer< MeshData >     Meshes      = GetResource(g_Meshes.index);
    StructuredBuffer< InstanceData > Instances   = GetResource(g_Instances.index);
    StructuredBuffer< uint >         IndexBuffer = GetResource(g_MeshStreams.indices);
    StructuredBuffer< Vertex >       VertexBuf   = GetResource(g_MeshStreams.vertices);
    StructuredBuffer< MaterialData > Materials   = GetResource(g_Materials.index);

    InstanceData instance = Instances[InstanceID()];
    MeshData     mesh     = Meshes[instance.meshID];

    bool bHasMaterial = instance.materialID != INVALID_INDEX;
    MaterialData mat  = (MaterialData)0;
    if (bHasMaterial)
        mat = Materials[instance.materialID];
    bool bUseFaceNormals = bHasMaterial && ((mat.materialFlags & MATERIAL_FLAG_FACE_NORMALS) != 0u);

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

    float3 geometricNOS = cross(p1OS - p0OS, p2OS - p0OS);
    float3 geometricNWorld = mul(transpose((float3x3)WorldToObject3x4()), geometricNOS);
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

    if (bUseFaceNormals)
        shadingNWorld = geometricNWorld;
    else if (dot(shadingNWorld, geometricNWorld) < 0.0)
        shadingNWorld = -shadingNWorld;

    float3 tangentOS =
        float3(v0.tangentX, v0.tangentY, v0.tangentZ) * bary.x +
        float3(v1.tangentX, v1.tangentY, v1.tangentZ) * bary.y +
        float3(v2.tangentX, v2.tangentY, v2.tangentZ) * bary.z;

    float2 uv =
        float2(v0.u, v0.v) * bary.x +
        float2(v1.u, v1.v) * bary.y +
        float2(v2.u, v2.v) * bary.z;

    float3 outwardShadingNWorld = bHasMaterial ? ReadShadingNormal(mat, uv, shadingNWorld, tangentOS) : shadingNWorld;
    if (dot(outwardShadingNWorld, geometricNWorld) < 0.0)
        outwardShadingNWorld = -outwardShadingNWorld;

    float3 faceShadingNWorld    = outwardShadingNWorld;
    float3 faceGeometricNWorld  = geometricNWorld;

    if (dot(faceGeometricNWorld, WorldRayDirection()) > 0.0)
        faceGeometricNWorld = -faceGeometricNWorld;
    if (dot(faceShadingNWorld, WorldRayDirection()) > 0.0)
        faceShadingNWorld = -faceShadingNWorld;
    if (dot(faceShadingNWorld, faceGeometricNWorld) < 0.0)
        faceShadingNWorld = -faceShadingNWorld;

    const float materialTransmission = bHasMaterial ? ReadTransmission(mat, uv) : 0.0;
    bool bUseOutwardNormal = materialTransmission > PT_LOBE_EPS;
    float3 surfaceNWorld   = bUseOutwardNormal ? outwardShadingNWorld : faceShadingNWorld;

    float3 tangentWorld = mul((float3x3)ObjectToWorld3x4(), tangentOS);
    tangentWorld -= surfaceNWorld * dot(tangentWorld, surfaceNWorld);
    float tangentLen2 = dot(tangentWorld, tangentWorld);
    if (tangentLen2 <= EPSILON_MIN)
    {
        float3 fallbackB;
        BuildSurfaceONB(surfaceNWorld, tangentWorld, fallbackB);
    }
    else
    {
        tangentWorld *= rsqrt(tangentLen2);
    }

    hp.hitKind = 1u;

    hp.albedo = bHasMaterial ? ReadBaseColor(mat, uv) : float3(1.0, 0.0, 1.0);
    hp.normal = surfaceNWorld;
    hp.dist   = RayTCurrent();

    hp.position        = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    hp.roughness       = bHasMaterial ? ReadRoughness(mat, uv) : 1.0;
    hp.tangent         = tangentWorld;
    hp.geometricNormal = geometricNWorld;

    hp.anisotropy         = bHasMaterial ? ReadAnisotropy(mat, uv) : 0.0;
    hp.anisotropyRotation = bHasMaterial ? mat.anisotropyRotation : 0.0;
    hp.emission           = bHasMaterial ? ReadEmission(mat, uv) : float3(0.0, 0.0, 0.0);
    hp.metallic           = bHasMaterial ? ReadMetallic(mat, uv) : 0.0;
    hp.specularColor      = bHasMaterial ? float3(mat.specularColorR, mat.specularColorG, mat.specularColorB) : float3(0.04, 0.04, 0.04);
    hp.ior                = bHasMaterial ? mat.ior : 1.0;
    hp.transmission       = materialTransmission;
    hp.clearcoat          = bHasMaterial ? mat.clearcoat : 0.0;
    hp.clearcoatRoughness = bHasMaterial ? mat.clearcoatRoughness : 0.0;
    hp.sheenColor         = bHasMaterial ? float3(mat.sheenColorR, mat.sheenColorG, mat.sheenColorB) : float3(0.0, 0.0, 0.0);
    hp.sheenRoughness     = bHasMaterial ? mat.sheenRoughness : 0.0;
    hp.specularStrength   = bHasMaterial ? mat.specularStrength : 1.0;

    hp.bPrincipled = (bHasMaterial && mat.materialType == PT_BSDF_PRINCIPLED) ? 1u : 0u;

    hp.materialID  = bHasMaterial ? instance.materialID : INVALID_INDEX;
    hp.instanceID  = InstanceID();
    hp.primitiveID = primIdx;
}




