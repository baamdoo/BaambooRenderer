#define _CAMERA
#define _MESH
#define _TRANSFORM
#define _MATERIAL
#define _MEDIUM
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
    uint   g_SobolSeed;
};

cbuffer PathSceneBounds : register(b0, space1)
{
    float3 g_SceneBoundsCenter;
    float  g_SceneBoundsRadius;
};

static const uint PT_MAX_MEDIUM_BOUNDARY_HITS = 256u;

RaytracingAccelerationStructure g_Scene : register(t0, space1);
StructuredBuffer< PrimaryRayMediumStackSeedData > g_PrimaryRayMediumSeed : register(t1, space1);

ConstantBuffer< DescriptorHeapIndex > g_Accumulation : register(b1,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Radiance     : register(b2,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_EnvironmentMap          : register(b18, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_EnvironmentDistribution : register(b19, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MaterialSlabs           : register(b20, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_LightSelectionCDF       : register(b22, ROOT_CONSTANT_SPACE);

#include "PathSurface.hlsli"
#include "PathComposite.hlsli"
#include "PathValidation.hlsli"
#include "PathSampling.hlsli"

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
    , uint recordValidationStats
    , out SurfaceData primaryHit
    , out PathContribution contribution
    , out PathBSDFSample primaryBSDFSample
#endif
)
{
    RayDesc ray = primaryRay;


    float3 L    = float3(0.0, 0.0, 0.0); // radiance carried back along this path
    float3 beta = float3(1.0, 1.0, 1.0); // β — path throughput: running product of f·cosθ/pdf

    float  rrEtaScale = 1.0;
#if PT_VALIDATION
    primaryHit        = (SurfaceData)0;
    contribution      = ZeroPathContribution();
    primaryBSDFSample = (PathBSDFSample)0;
#endif

    MediumStack hostStack, airStack;
    if (!TryLoadPrimaryMediumState(g_PrimaryRayMediumSeed[0], hostStack, airStack))
        return L;

    StructuredBuffer< InstanceData > Instances = GetResource(g_Instances.index);

    uint2 dimensions = DispatchRaysDimensions().xy;
    float tanHalfU = rcp(max(abs(g_Camera.mProj[0][0]) * float(dimensions.x), EPSILON_MIN));
    float tanHalfV = rcp(max(abs(g_Camera.mProj[1][1]) * float(dimensions.y), EPSILON_MIN));

    RayCone rayCone;
    rayCone.radius = 0.0;
    rayCone.tanHalfAngle = max(tanHalfU, tanHalfV);

    uint  prevBSDFFlags   = 0u;
    float prevMarginalPDF = 0.0;
    uint  wasDelta        = 0u;
    // A pure boundary moves ray.Origin without creating a new sampling vertex.
    float3 prevVertexPositionWS = primaryRay.Origin;
    uint boundaryCrossings = 0u;

    RngState alphaRng = InitRng(uint2(0, 0), rng.seed ^ 0x414C5048u, 0u);

    uint maxDepth = clamp(g_MaxDepth, 1u, PT_MAX_DEPTH_LIMIT);
    [loop]
    for (uint depth = 0u; depth < maxDepth;)
    {
        bool isPrimary = depth == 0u;

        SurfaceData hp     = (SurfaceData)0;
        SurfaceMaterial sm = (SurfaceMaterial)0;
        uint materialID     = INVALID_INDEX;

        float hitConeRadius = rayCone.radius;

        AnalyticSphereLightHit sphereHit;
        bool hasSphereHit = FindClosestAnalyticSphereLight(ray, sphereHit);
        RayDesc surfaceRay = ray;
        if (hasSphereHit)
            surfaceRay.TMax = sphereHit.dist;

        [loop]
        for (;;)
        {
            if (surfaceRay.TMin > surfaceRay.TMax)
            {
                hp = (SurfaceData)0;
                break;
            }
            SurfacePayload sp = (SurfacePayload)0;
            TraceRay(g_Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, surfaceRay, sp);

            if (sp.hitKind == 0u || (hasSphereHit && sphereHit.dist <= sp.dist))
            {
                hp = (SurfaceData)0;
                break;
            }
            hitConeRadius = rayCone.radius + sp.dist * rayCone.tanHalfAngle;

            hp = ResolvePathSurface(sp, ray.Direction, abs(hitConeRadius));

            materialID = Instances[hp.instanceID].materialID;
            StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);
            if (materialID != INVALID_INDEX && (Materials[materialID].materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u)
            {
                // Artificial enclosures have no surface texture/BSDF/alpha evaluation.
                sm.materialFlags = Materials[materialID].materialFlags;
                sm.layerCount = max(Materials[materialID].layerCount, 1u);
                break;
            }
            sm = LoadSurfaceMaterial(materialID, hp.uv, hp.ddxUV, hp.ddyUV, hp.tangent.w);

            bool isAlphaBlend = (sm.materialFlags & MATERIAL_FLAG_ALPHA_BLEND) != 0;
            bool isNullBranch = isAlphaBlend && NextFloat(alphaRng) >= sm.opacity;

            if (!isNullBranch)
            {
                OrientOpaqueSurfaceNormalForPath(hp, sm, -ray.Direction);
                break;
            }

            // Skip only the surface query; medium transport retains the full segment.
            surfaceRay.TMin = hp.dist + PT_RAY_EPS;
        }

        float segmentEndDistance = hp.hitKind != 0u ? hp.dist : surfaceRay.TMax;

        // The geometric endpoint is provisional until medium transport reaches it.
        // Reference: https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators
        Medium currentMedium = hostStack.Current().medium;
        
        MediumStack media; media.Initialize();
        if (hostStack.count > 1u)
        {
            if (currentMedium.mediumID != INVALID_INDEX)
                media.Initialize(currentMedium);
        }
        else if (currentMedium.mediumID == INVALID_INDEX)
        {
            media = airStack;
        }
        else
        {
            if (airStack.count != 0u)
                break;

            media.Initialize(currentMedium);
        }

        if (media.count != 0u)
        {
            uint mediumSeed = rng.seed ^ 0x4D454449u;
            if (boundaryCrossings != 0u)
                mediumSeed = PCGHash(mediumSeed ^ PCGHash(boundaryCrossings));
            RngState mediumRng = InitRng(uint2(0, 0), mediumSeed, depth);

            MediumInteractionSample mediumSample = (MediumInteractionSample)0;
            if (!SampleMediumInteraction(media, ray.Origin, ray.Direction, segmentEndDistance, mediumRng, mediumSample))
            {
                break;
            }

            float3 betaAfterMedium = beta * mediumSample.weight;
            if (!IsPathFinite3(betaAfterMedium))
                break;
            beta = betaAfterMedium;

            if (mediumSample.eventType == MEDIUM_EVENT_SCATTER)
            {
                float3 scatterPosition = ray.Origin + mediumSample.distance * ray.Direction;
                if (!IsPathFinite3(scatterPosition))
                    break;

                bool canContinue = (depth + 1u) < maxDepth;

                rayCone.radius += mediumSample.distance * rayCone.tanHalfAngle;

                // No surface exists at this event
                SurfaceData noSurface         = (SurfaceData)0;
                SurfaceMaterial noMaterial    = (SurfaceMaterial)0;
                BoundaryMediumPair noBoundary = (BoundaryMediumPair)0;
                uint directQuerySeed = PCGHash(rng.seed ^ PCGHash(depth + 0x9E3779B9u));
#if PT_VALIDATION
                PathContribution directContribution;
#endif
                float3 neeContribution;
                if (!SampleLd(scatterPosition, -ray.Direction, noSurface, noMaterial, noBoundary,
                        media, hostStack, airStack, false, depth, directQuerySeed, canContinue, rng, neeContribution
#if PT_VALIDATION
                        , recordValidationStats, directContribution
#endif
                    ))
                    break;

                float3 mediumContribution = beta * neeContribution;
                if (!IsPathFinite3(mediumContribution))
                    break;
                L += mediumContribution;
                if (!canContinue)
                    break;

                SobolRebaseForBounce(rng, depth, SOBOL_SECTION_BSDF);
                float  constituentU = NextFloat(rng);
                float2 directionU   = NextFloat2(rng);
                
                MediumPhaseSample phaseSample = (MediumPhaseSample)0;
                bool isPhaseValid = SampleMediumPhase(media, scatterPosition, ray.Direction, float3(constituentU, directionU), phaseSample); // Incoming-direction
                if (!isPhaseValid)
                    break;

                float3 betaAfterPhase = beta * phaseSample.value / phaseSample.pdf;
                if (!IsPathFinite3(betaAfterPhase))
                    break;
                beta = betaAfterPhase;

                // medium scatter changes no IOR.
                const float rrThreshold = 0.05;
                if (depth >= 3u)
                {
                    SobolRebaseForBounce(rng, depth, SOBOL_SECTION_RR);
                    float3 rrBeta = beta * rrEtaScale;
                    float qSurvive = clamp(max(rrBeta.r, max(rrBeta.g, rrBeta.b)), rrThreshold, 1.0 - rrThreshold);
                    if (NextFloat(rng) >= qSurvive)
                        break;
                    beta /= qSurvive;
                }

                wasDelta             = 0u;
                prevBSDFFlags        = 0u;
                prevMarginalPDF      = phaseSample.pdf;
                prevVertexPositionWS = scatterPosition;

                ray.Origin    = scatterPosition;
                ray.Direction = phaseSample.directionWS;
                ray.TMin      = 0.0; // There is no surface at this volume event to offset from.
                ray.TMax      = GetSceneRayLength(ray.Origin, g_SceneBoundsCenter, g_SceneBoundsRadius);
                if (ray.TMax <= ray.TMin)
                    break;
                // Keep the medium stack; discard the provisional geometric endpoint.
                ++depth;

                continue;
            }
            else if (mediumSample.eventType == MEDIUM_EVENT_ABSORB)
            {
                break;
            }
        }

        // ── Case B: no mesh — reach the analytic emitter or environment ──
        // Medium transport must reach this endpoint before emission is added.
        if (hp.hitKind == 0u)
        {
            float3 emittedContribution;
            if (hasSphereHit)
            {
                float3 lightPosition = ray.Origin + segmentEndDistance * ray.Direction;
                emittedContribution = beta * EvaluateAnalyticSphereEmission(
                    sphereHit, prevVertexPositionWS, lightPosition, ray.Direction,
                    depth, prevMarginalPDF, wasDelta);
            }
            else
            {
                float misWeight = (depth > 0u)
                    ? CalculateMISWeight(prevMarginalPDF, EnvironmentPDF(ray.Direction), wasDelta)
                    : 1.0; // depth 0: camera ray looked straight at the sky
                emittedContribution = beta * EvaluateEnvironmentRadiance(ray.Direction) * misWeight;
            }

            if (IsPathFinite3(emittedContribution))
            {
                L += emittedContribution;
#if PT_VALIDATION
                AccumulatePathContribution(contribution, prevBSDFFlags, emittedContribution);
#endif
            }
            // Black/back-facing spheres also terminate the path and hide the sky.
            break;
        }

        rayCone.radius = hitConeRadius;

        const float3 surfacePosition = ray.Origin + ray.Direction * hp.dist;
        // Enter to the medium boundary
        if ((sm.materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u)
        {
            // Reference: https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators
            if (++boundaryCrossings > PT_MAX_MEDIUM_BOUNDARY_HITS || sm.layerCount != 1u ||
                (sm.materialFlags & (MATERIAL_FLAG_ALPHA_MASK | MATERIAL_FLAG_ALPHA_BLEND | MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_RELATIVE_IOR_INTERFACE)) != 0u)
                break;

            MediumData interiorData;
            Medium toMedium;
            toMedium.mediumID = Instances[hp.instanceID].mediumID;
            toMedium.ior      = currentMedium.ior; // A pure boundary does not change IOR.

            if (!TryLoadMedium(toMedium.mediumID, interiorData) ||
                !TryCrossPureMediumBoundary(hp.instanceID, toMedium, interiorData.mediumFlags, dot(hp.geometricNormal, -ray.Direction), hostStack, airStack))
                break;

            float remainingSegmentLength = ray.TMax - hp.dist;
            if (!IsPathFinite3(surfacePosition) || remainingSegmentLength <= PT_RAY_EPS)
                break;

            ray.Origin = surfacePosition;
            ray.TMax   = remainingSegmentLength;
            ray.TMin   = PT_RAY_EPS;
            continue;
        }

#if PT_VALIDATION
        if (depth == 0u)
            primaryHit = hp;
#endif
        // ── Case A: the random walk hit an emitter ─────────────────────
        if (any(sm.emission > 0.0) && dot(hp.geometricNormal, -ray.Direction) > 0.0)
        {
            float3 emittedContribution = beta * sm.emission;
            if (depth == 0u)
            {
                if (IsPathFinite3(emittedContribution))
                    L += emittedContribution;
            }
            else
            {
                float pdfLight  = AreaLightPDFAtHit(prevVertexPositionWS, surfacePosition, ray.Direction);
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

        const float NgoWo = dot(hp.geometricNormal, woWS);
        if (!IsPathFinite(NgoWo) || abs(NgoWo) <= EPSILON_MIN)
            break;

        const bool shouldPreservesPathMedium = IsThinWalled(sm) || IsRelativeIORInterface(sm);
        bool canChangePathMedium = false;

        BoundaryMediumPair boundaryPair;
        if (shouldPreservesPathMedium)
        {
            boundaryPair.mediumI    = hostStack.Current().medium;
            boundaryPair.mediumT    = boundaryPair.mediumI;
            boundaryPair.isEntering = NgoWo > 0.0 ? 1u : 0u;
        }
        else
        {
            Medium toMedium;
            if (!BxDF::LayerComposite::TryResolveTerminalMedium(
                    sm,
                    Instances[hp.instanceID].mediumID,
                    hp.uv,
                    hp.ddxUV,
                    hp.ddyUV,
                    toMedium,
                    canChangePathMedium))
            {
                break;
            }

            if (!canChangePathMedium)
            {
                boundaryPair.mediumI    = hostStack.Current().medium;
                boundaryPair.mediumT    = boundaryPair.mediumI;
                boundaryPair.isEntering = 1u;
            }
            else if (!hostStack.TryResolveBoundaryMediums(
                         hp.instanceID,
                         toMedium,
                         NgoWo,
                         boundaryPair))
            {
                break;
            }
        }

        // Fixed-endpoint queries must not consume or depend on the transport RNG counter.
        uint directionalQuerySeed = PCGHash(rng.seed ^ PCGHash(depth + 0x9E3779B9u) ^ PCGHash(materialID + 0x85EBCA6Bu));
#if PT_VALIDATION
        PathContribution directContribution;
#endif
        float3 directLighting;
        if (!SampleLd(surfacePosition, woWS, hp, sm, boundaryPair,
                media, hostStack, airStack, canChangePathMedium, depth, directionalQuerySeed,
                (depth + 1u) < maxDepth, rng, directLighting
#if PT_VALIDATION
                , recordValidationStats, directContribution
#endif
            ))
            break;
        float3 directContributionSum = beta * directLighting;
        if (IsPathFinite3(directContributionSum))
            L += directContributionSum;
#if PT_VALIDATION
        contribution.diffuse      += beta * directContribution.diffuse;
        contribution.specular     += beta * directContribution.specular;
        contribution.transmission += beta * directContribution.transmission;
#endif

        RayCone sampledCone = rayCone;

        float3 wo = BxDF::ToLocal(frame, woWS);
#if PT_VALIDATION
        BxDF::LayerWalkerAudit sampleAudit;
#endif
        // History-space continuation query: weight is already C_H / q_H.
        SobolRebaseForBounce(rng, depth, SOBOL_SECTION_BSDF);
        PathBSDFSample s = BxDF::LayerComposite::SampleRay(
            sm,
            hp.uv,
            hp.ddxUV,
            hp.ddyUV,
            wo,
            boundaryPair,
            3u,  // internal layer-event depth, independent of outer path depth
            isPrimary ? 0.25 : 1.0,
            sampledCone,
            rng
#if PT_VALIDATION
            , sampleAudit
#endif
        );
#if PT_VALIDATION
        if (recordValidationStats != 0u)
            RecordValidationWalkerAudit(PT_VALIDATION_WALKER_SAMPLE, sm.layerCount, sampleAudit);
#endif

        float marginalPDF = 0.0;
        if (s.valid != 0u && s.isDelta == 0u)
        {
#if PT_VALIDATION
            BxDF::MarginalPDFAudit marginalAudit;
#endif
            // Direction-space query used only by outer MIS
            marginalPDF = BxDF::DirectionalComposite::MarginalPDF(
                sm,
                hp.uv,
                hp.ddxUV,
                hp.ddyUV,
                wo,
                s.wi,
                boundaryPair,
                directionalQuerySeed
#if PT_VALIDATION
                , marginalAudit
#endif
            );
#if PT_VALIDATION
            if (recordValidationStats != 0u)
            {
                uint retryClassification = DiagnoseValidationMarginalPDFZero(
                    sm,
                    hp.uv,
                    hp.ddxUV,
                    hp.ddyUV,
                    wo,
                    s.wi,
                    boundaryPair,
                    directionalQuerySeed,
                    marginalAudit);
                RecordValidationMarginalPDF(
                    PT_VALIDATION_QUERY_CONTINUATION,
                    sm.layerCount,
                    marginalPDF,
                    marginalAudit,
                    retryClassification);
            }
#endif
        }
#if PT_VALIDATION
        if (depth == 0u)
            primaryBSDFSample = s;
#endif
        if (s.valid == 0u)
            break;

        float3 wiWSUnnormalized = BxDF::ToWorld(frame, s.wi);
        float  wiLength2        = dot(wiWSUnnormalized, wiWSUnnormalized);
        if (!IsPathFinite(wiLength2) || wiLength2 <= EPSILON_MIN)
        {
            break;
        }
        float3 wiWS = wiWSUnnormalized * rsqrt(wiLength2);

        const float NgoWi = dot(hp.geometricNormal, wiWS);
        if (!IsPathFinite(NgoWi) || abs(NgoWi) <= EPSILON_MIN)
            break;

        float3 nextBeta       = beta * s.weight;
        float  nextRrEtaScale = rrEtaScale * s.rrEtaScale;
        if (!IsPathFinite3(nextBeta) || !IsPathFinite(nextRrEtaScale))
            break;

        // ===== medium stack update ===============================
        const bool crossedSurface        = (NgoWo > 0.0) != (NgoWi > 0.0);
        const bool crossedSampledSurface = (wo.z > 0.0) != (s.wi.z > 0.0);
        if (crossedSampledSurface != crossedSurface)
            break;

        const bool changesPathMedium = crossedSurface && canChangePathMedium;
        if (changesPathMedium)
        {
            bool committed;
            if (boundaryPair.isEntering != 0u)
            {
                MediumEntry entry;
                entry.boundaryInstanceID = hp.instanceID;
                entry.medium             = boundaryPair.mediumT;

                committed = hostStack.TryPush(entry);
            }
            else
            {
                committed = hostStack.TryPop(hp.instanceID);
            }

            if (!committed)
                break;
        }

        // ===== β update ===============================
        beta       = nextBeta;
        rrEtaScale = nextRrEtaScale;

        // ===== Russian Roulette ========================================
        const float rrThreshold = 0.05;
        if (depth >= 3)
        {
            SobolRebaseForBounce(rng, depth, SOBOL_SECTION_RR);
            float3 rrBeta  = beta * rrEtaScale;
            float qSurvive = clamp(max(rrBeta.r, max(rrBeta.g, rrBeta.b)), rrThreshold, 1.0 - rrThreshold);
            if (NextFloat(rng) >= qSurvive)
                break;

            beta /= qSurvive;
        }

        wasDelta             = s.isDelta;
        prevBSDFFlags        = s.flags;
        prevMarginalPDF      = marginalPDF;
        prevVertexPositionWS = surfacePosition;

        rayCone = sampledCone;

        ray.Origin    = OffsetRay(surfacePosition, hp.geometricNormal, wiWS);
        ray.Direction = wiWS;
        ray.TMin      = PT_RAY_EPS;
        ray.TMax      = GetSceneRayLength(ray.Origin, g_SceneBoundsCenter, g_SceneBoundsRadius);
        if (ray.TMax <= ray.TMin)
            break;
        ++depth;
    }

    return L;
}

[shader("raygeneration")]
void RayGen()
{
    RWTexture2D< float4 > Accumulation = GetResource(g_Accumulation.index);
    RWTexture2D< float4 > Radiance     = GetResource(g_Radiance.index);

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
        RngState rng = InitSobolRng(rayIndex, g_SobolSeed, sampleIndex);

        float2 pixelJitter = SamplePixelTent(NextFloat2(rng));
        float2 pathUV      = (float2(rayIndex) + 0.5 + pixelJitter) / float2(rayDimensions);
        float3 pathTarget  = ReconstructWorldPos(pathUV, 1.0, g_Camera.mViewProjInv);

        ray.Origin    = g_Camera.posWORLD;
        ray.Direction = normalize(pathTarget - g_Camera.posWORLD);
        ray.TMin      = g_Camera.zNear;
        // Physical transport uses the complete bounded scene
        ray.TMax      = GetSceneRayLength(ray.Origin, g_SceneBoundsCenter, g_SceneBoundsRadius);
        if (ray.TMax <= ray.TMin)
            continue;

#if PT_VALIDATION
        SurfaceData primaryHit = (SurfaceData)0;

        PathContribution pathContribution;
        PathBSDFSample primaryBSDFSample = (PathBSDFSample)0;

        float3 sampleRadiance = TracePath(ray, rng, 1u, primaryHit, pathContribution, primaryBSDFSample);
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
    PathValidationSums primaryValidation = TracePrimaryValidation(rayIndex, rayDimensions);
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
    WritePathValidationAOVs(rayIndex, validationSums, primaryValidation, bReset, previousAccumulation.a, invAccumulatedSamples);
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
