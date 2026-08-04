#define _CAMERA
#define _MESH
#define _TRANSFORM
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
ConstantBuffer< DescriptorHeapIndex > g_Albedo                : register(b3,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Normal                : register(b4,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Depth                 : register(b5,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_GeometricNormal       : register(b6,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MaterialParams        : register(b7,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MaterialExtra         : register(b8,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MaterialSpecularColor : register(b9,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Emission              : register(b10, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_DiffuseRadiance       : register(b11, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SpecularRadiance      : register(b12, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TransmissionRadiance  : register(b13, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SurfaceLobeMask       : register(b14, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SurfaceLobeWeight     : register(b15, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SampledLobeFrequency  : register(b16, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_PrimaryId             : register(b17, ROOT_CONSTANT_SPACE);
#endif // PT_VALIDATION
ConstantBuffer< DescriptorHeapIndex > g_EnvironmentMap          : register(b18, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_EnvironmentDistribution : register(b19, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MaterialSlabs           : register(b20, ROOT_CONSTANT_SPACE);

#include "PathSurface.hlsli"
#include "PathComposite.hlsli"
#include "PathSampling.hlsli"
#include "PathValidation.hlsli"

void OrientOpaqueSurfaceNormalForPath(inout SurfaceData hp, inout SurfaceMaterial material, float3 woWS)
{
    if (HasTransmissionLobe(material) || HasCoatingLayers(material))
        return;

    if (dot(hp.geometricNormal, woWS) < 0.0)
    {
        hp.normal = -hp.normal;
        hp.tangent.w = -hp.tangent.w;
        material.tangentFrameSign = -material.tangentFrameSign;
    }
}

float3 TracePath(RayDesc primaryRay, inout RngState rng
#if PT_VALIDATION
    , out SurfaceData primaryHit
    , out PathContribution contribution
    , out PathBSDFSample primaryBSDFSample
#endif
)
{
    RayDesc ray = primaryRay;
    StructuredBuffer< InstanceData > Instances = GetResource(g_Instances.index);

    float3 L    = float3(0.0, 0.0, 0.0); // radiance carried back along this path
    float3 beta = float3(1.0, 1.0, 1.0); // β — path throughput: running product of f·cosθ/pdf

    float  rrEtaScale = 1.0;
#if PT_VALIDATION
    primaryHit        = (SurfaceData)0;
    contribution      = ZeroPathContribution();
    primaryBSDFSample = (PathBSDFSample)0;
#endif

    uint2 dimensions = DispatchRaysDimensions().xy;
    float tanHalfU = rcp(max(abs(g_Camera.mProj[0][0]) * float(dimensions.x), EPSILON_MIN));
    float tanHalfV = rcp(max(abs(g_Camera.mProj[1][1]) * float(dimensions.y), EPSILON_MIN));

    RayCone rayCone;
    rayCone.radius = 0.0;
    rayCone.tanHalfAngle = max(tanHalfU, tanHalfV);

    uint  prevBSDFFlags   = 0u;
    float prevMarginalPDF = 0.0;
    uint  wasDelta        = 0u;

    uint maxDepth = clamp(g_MaxDepth, 1u, PT_MAX_DEPTH_LIMIT);
    [loop]
    for (uint depth = 0u; depth < maxDepth; ++depth)
    {
        bool isPrimary = depth == 0u;

        SurfaceData hp     = (SurfaceData)0;
        SurfaceMaterial sm = (SurfaceMaterial)0;
        uint materialID     = INVALID_INDEX;

        float hitConeRadius = rayCone.radius;

        [loop]
        for (;;)
        {
            SurfacePayload sp = (SurfacePayload)0;
            TraceRay(g_Scene, RAY_FLAG_FORCE_NON_OPAQUE, 0xFF, 0, 0, 0, ray, sp);

            if (sp.hitKind == 0u)
            {
                hp = (SurfaceData)0;
                break;
            }
            hitConeRadius = rayCone.radius + sp.dist * rayCone.tanHalfAngle;

            hp = ResolvePathSurface(sp, ray.Direction, abs(hitConeRadius));

            materialID = Instances[hp.instanceID].materialID;
            sm = LoadSurfaceMaterial(materialID, hp.uv, hp.ddxUV, hp.ddyUV, hp.tangent.w);

            bool isAlphaBlend = (sm.materialFlags & MATERIAL_FLAG_ALPHA_BLEND) != 0;
            bool isNullBranch = isAlphaBlend && NextFloat(rng) >= sm.opacity;

            if (!isNullBranch)
            {
                OrientOpaqueSurfaceNormalForPath(hp, sm, -ray.Direction);
                break;
            }

            // null intersection event: only ray segment range(min) updated for next trace
            ray.TMin = hp.dist + PT_RAY_EPS;
        }

#if PT_VALIDATION
        if (depth == 0u)
            primaryHit = hp;
#endif
        // ── Case B: miss — the path escapes to the environment ─────────
        if (hp.hitKind == 0u)
        {
            float misWeight = (depth > 0u)
                ? CalculateMISWeight(prevMarginalPDF, EnvironmentPDF(ray.Direction), wasDelta)
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

        rayCone.radius = hitConeRadius;

        const float3 surfacePosition = ray.Origin + ray.Direction * hp.dist;
        // ── Case A: the random walk hit an emitter ─────────────────────
        if (any(sm.emission > 0.0))
        {
            float3 emittedContribution = beta * sm.emission;
            if (depth == 0u)
            {
                if (IsPathFinite3(emittedContribution))
                    L += emittedContribution;
            }
            else
            {
                float pdfLight  = AreaLightPDFAtHit(ray.Origin, surfacePosition, ray.Direction);
                float misWeight = CalculateMISWeight(prevMarginalPDF, pdfLight, wasDelta);
                emittedContribution *= misWeight;

                if (IsPathFinite3(emittedContribution))
                {
                    L += emittedContribution;
#if PT_VALIDATION
                    AccumulatePathContribution(contribution, prevBSDFFlags, emittedContribution);
#endif
                }
            }
        }

        // ── Case C: surface hit — NEE, then extend the walk ────────────
        BxDF::Frame frame = MakeSurfaceFrame(hp);
        float3 woWS = -ray.Direction;

        const float etaExterior = 1.0; // replace with path medium state when it exists
        // Fixed-endpoint queries must not consume or depend on the transport RNG counter.
        uint directionalQuerySeed = PCGHash(rng.seed ^ PCGHash(depth + 0x9E3779B9u) ^ PCGHash(materialID + 0x85EBCA6Bu));
#if PT_VALIDATION
        PathContribution directContribution;
        float3 directLighting = EstimateDirectLighting(
            surfacePosition,
            hp.geometricNormal,
            frame,
            woWS,
            sm,
            hp.uv,
            hp.ddxUV,
            hp.ddyUV,
            etaExterior,
            directionalQuerySeed,
            (depth + 1u) < maxDepth,
            rng,
            directContribution);

        PathContribution environmentDirectContribution;
        float3 environmentDirectLighting = EstimateEnvironmentDirectLighting(
            surfacePosition,
            hp.geometricNormal,
            frame,
            woWS,
            sm,
            hp.uv,
            hp.ddxUV,
            hp.ddyUV,
            etaExterior,
            directionalQuerySeed,
            (depth + 1u) < maxDepth,
            rng,
            environmentDirectContribution);
#else
        float3 directLighting = EstimateDirectLighting(
            surfacePosition,
            hp.geometricNormal,
            frame,
            woWS,
            sm,
            hp.uv,
            hp.ddxUV,
            hp.ddyUV,
            etaExterior,
            directionalQuerySeed,
            (depth + 1u) < maxDepth,
            rng);
        float3 environmentDirectLighting = EstimateEnvironmentDirectLighting(
            surfacePosition,
            hp.geometricNormal,
            frame,
            woWS,
            sm,
            hp.uv,
            hp.ddxUV,
            hp.ddyUV,
            etaExterior,
            directionalQuerySeed,
            (depth + 1u) < maxDepth,
            rng);
#endif
        float3 directContributionSum = beta * (directLighting + environmentDirectLighting);
        if (IsPathFinite3(directContributionSum))
            L += directContributionSum;
#if PT_VALIDATION
        contribution.diffuse      += beta * (directContribution.diffuse + environmentDirectContribution.diffuse);
        contribution.specular     += beta * (directContribution.specular + environmentDirectContribution.specular);
        contribution.transmission += beta * (directContribution.transmission + environmentDirectContribution.transmission);
#endif

        RayCone sampledCone = rayCone;

        float3 wo = BxDF::ToLocal(frame, woWS);
        // History-space continuation query: weight is already C_H / q_H.
        PathBSDFSample s = BxDF::LayerComposite::SampleRay(
            sm,
            hp.uv,
            hp.ddxUV,
            hp.ddyUV,
            wo,
            etaExterior,
            3u,  // internal layer-event depth, independent of outer path depth
            isPrimary ? 0.25 : 1.0,
            sampledCone,
            rng);

        float marginalPDF = 0.0;
        if (s.valid != 0u && s.isDelta == 0u)
        {
            // Direction-space query used only by outer MIS
            marginalPDF = BxDF::DirectionalComposite::MarginalPDF(
                sm,
                hp.uv,
                hp.ddxUV,
                hp.ddyUV,
                wo,
                s.wi,
                etaExterior,
                directionalQuerySeed);
        }
#if PT_VALIDATION
        if (depth == 0u)
            primaryBSDFSample = s;
#endif
        if (s.valid == 0u)
            break;

        if (!IsPathFinite3(s.wi) || !IsPathFinite3(s.weight) || !IsPathFinite(marginalPDF))
            break;

        if (s.isDelta == 0u && marginalPDF <= 0.0)
            break;

        // ===== β update ===============================
        beta *= s.weight;
        if (!IsPathFinite3(beta))
            break;
        rrEtaScale *= s.rrEtaScale;

        prevBSDFFlags   = s.flags;
        prevMarginalPDF = marginalPDF;
        wasDelta        = s.isDelta;

        // ===== Russian Roulette ========================================
        const float rrThreshold = 0.05;
        if (depth >= 3)
        {
            float3 rrBeta  = beta * rrEtaScale;
            float qSurvive = clamp(max(rrBeta.r, max(rrBeta.g, rrBeta.b)), rrThreshold, 1.0 - rrThreshold);
            if (NextFloat(rng) >= qSurvive)
                break;

            beta /= qSurvive;
        }

        float3 wiWS = normalize(BxDF::ToWorld(frame, s.wi));
        if (!IsPathFinite3(wiWS))
            break;

        rayCone = sampledCone;

        ray.Origin    = OffsetRay(surfacePosition, hp.geometricNormal, wiWS);
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
        SurfaceData primaryHit = (SurfaceData)0;

        PathContribution pathContribution;
        PathBSDFSample primaryBSDFSample = (PathBSDFSample)0;

        float3 sampleRadiance = TracePath(ray, rng, primaryHit, pathContribution, primaryBSDFSample);
        if (IsPathFinite3(sampleRadiance))
            Lsum += sampleRadiance;

        AccumulateValidationContribution(validationSums, pathContribution);

        if (primaryHit.hitKind != 0u)
            validationSums.sampledLobeFrequency += BxDF::LayerComposite::SampledLobeVector(primaryBSDFSample);
#else
        float3 sampleRadiance = TracePath(ray, rng);
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

        SurfaceData primaryValidationHit = (SurfaceData)0;
        PathContribution unusedPrimaryContribution;
        PathBSDFSample primaryValidationBSDFSample = (PathBSDFSample)0;
        RngState validationRng = InitRng(rayIndex, 0u, 0u);
        validationRng.seed = PCGHash(validationRng.seed ^ 0xD1B54A35u);
        TracePath(primaryValidationRay, validationRng, primaryValidationHit, unusedPrimaryContribution, primaryValidationBSDFSample);

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
    PrimaryIdAOV[rayIndex]            = float4(primaryValidation.primaryId, 1.0);
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

bool AlphaTestRejectsHit(in BuiltInTriangleIntersectionAttributes attr, bool bSampleBlend, uint seed)
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

    bool isMask  = (mat.materialFlags & MATERIAL_FLAG_ALPHA_MASK) != 0u;
    bool isBlend = (mat.materialFlags & MATERIAL_FLAG_ALPHA_BLEND) != 0u;
    if (!isMask && !(bSampleBlend && isBlend))
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

    float opacity = ReadOpacityLevel(mat, uv);
    if (isMask)
        return opacity < mat.alphaCutoff;

    uint opacitySeed = PCGHash(
        seed ^
        PCGHash(InstanceID() + 0x9E3779B9u) ^
        PCGHash(PrimitiveIndex() + 0x85EBCA6Bu) ^
        PCGHash(asuint(RayTCurrent()) + 0xC2B2AE35u));
    float u = UintToUnitFloat(opacitySeed);

    return u >= opacity;
}

[shader("anyhit")]
void AnyHit_PrimaryAlpha(inout SurfacePayload hp, in BuiltInTriangleIntersectionAttributes attr)
{
    if (AlphaTestRejectsHit(attr, false, 0u))
        IgnoreHit();
}

[shader("anyhit")]
void AnyHit_ShadowAlpha(inout ShadowPayload sp, in BuiltInTriangleIntersectionAttributes attr)
{
    if (AlphaTestRejectsHit(attr, true, sp.alphaSeed))
        IgnoreHit();
}

[shader("closesthit")]
void ClosestHit_Primary(inout SurfacePayload hp, in BuiltInTriangleIntersectionAttributes attr)
{
    hp.barycentrics = attr.barycentrics;
    hp.dist         = RayTCurrent();
    hp.hitKind      = 1u;
    hp.instanceID   = InstanceID();
    hp.primitiveID  = PrimitiveIndex();
}
