#include "ShaderShared/Scene.h"
#include "ShaderShared/PathTracer.h"
#include "Common/Binding.slangh"
#include "Common/PathSampling.slangh"

struct PathTracerPush
{
    PathTracerConstants constants;

    uint ID_Scene;
    uint ID_PrimaryRayMediumSeed;
    uint ID_Accumulation;
    uint ID_Radiance;
    uint ID_EnvironmentMap;
    uint ID_EnvironmentDistribution;
    uint ID_MaterialSlabs;
    uint ID_LightSelectionCDF;

#if PT_VALIDATION
    uint ID_PathValidationStats;
    uint ID_Albedo;
    uint ID_Normal;
    uint ID_Depth;
    uint ID_GeometricNormal;
    uint ID_MaterialParams;
    uint ID_MaterialExtra;
    uint ID_MaterialSpecularColor;
    uint ID_Emission;
    uint ID_DiffuseRadiance;
    uint ID_SpecularRadiance;
    uint ID_TransmissionRadiance;
    uint ID_SurfaceLobeMask;
    uint ID_SurfaceLobeWeight;
    uint ID_SampledLobeFrequency;
    uint ID_PrimaryId;
#endif
};

[[vk::push_constant]] ConstantBuffer< PathTracerPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

static const uint PT_MAX_MEDIUM_BOUNDARY_HITS = 256u;

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

float3 TracePath(PathScene scene, RayDesc primaryRay, inout RngState rng
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
    primaryHit        = {};
    contribution      = ZeroPathContribution();
    primaryBSDFSample = {};
#endif

    MediumStack hostStack, airStack;
    if (!TryLoadPrimaryMediumState(scene.primaryMediumSeed[0], hostStack, airStack))
        return L;

    RaytracingAccelerationStructure  tlas      = GetResource(scene.buffers.tlas);
    StructuredBuffer< InstanceData > instances = GetResource(scene.buffers.instances);
    StructuredBuffer< MaterialData > materials = GetResource(scene.buffers.materials);

    uint2 dimensions = DispatchRaysDimensions().xy;
    float tanHalfU = rcp(max(abs(scene.camera.mProj[0][0]) * float(dimensions.x), EPSILON_MIN));
    float tanHalfV = rcp(max(abs(scene.camera.mProj[1][1]) * float(dimensions.y), EPSILON_MIN));

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

    uint maxDepth = clamp(scene.maxDepth, 1u, PT_MAX_DEPTH_LIMIT);
    [loop]
    for (uint depth = 0u; depth < maxDepth;)
    {
        bool isPrimary = depth == 0u;

        SurfaceData hp     = {};
        SurfaceMaterial sm = {};
        uint materialID     = kInvalidIndex;

        float hitConeRadius = rayCone.radius;

        AnalyticSphereLightHit sphereHit;
        bool hasSphereHit = FindClosestAnalyticSphereLight(scene.buffers, ray, sphereHit);
        RayDesc surfaceRay = ray;
        if (hasSphereHit)
            surfaceRay.TMax = sphereHit.dist;

        [loop]
        for (;;)
        {
            if (surfaceRay.TMin > surfaceRay.TMax)
            {
                hp = {};
                break;
            }
            SurfacePayload sp = {};
            TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, surfaceRay, sp);

            if (sp.hitKind == 0u || (hasSphereHit && sphereHit.dist <= sp.dist))
            {
                hp = {};
                break;
            }
            hitConeRadius = rayCone.radius + sp.dist * rayCone.tanHalfAngle;

            hp = ResolvePathSurface(scene.buffers, sp, ray.Direction, abs(hitConeRadius));

            materialID = instances[hp.instanceID].materialID;
            if (materialID != kInvalidIndex && (materials[materialID].materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u)
            {
                // Artificial enclosures have no surface texture/BSDF/alpha evaluation.
                sm.materialFlags = materials[materialID].materialFlags;
                sm.layerCount = max(materials[materialID].layerCount, 1u);
                break;
            }
            sm = LoadSurfaceMaterial(scene.buffers, materialID, hp.uv, hp.ddxUV, hp.ddyUV, hp.tangent.w);

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
            if (currentMedium.mediumID != kInvalidIndex)
                media.Initialize(currentMedium);
        }
        else if (currentMedium.mediumID == kInvalidIndex)
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

            MediumInteractionSample mediumSample = {};
            if (!SampleMediumInteraction(scene.buffers, media, ray.Origin, ray.Direction, segmentEndDistance, mediumRng, mediumSample))
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
                SurfaceData noSurface         = {};
                SurfaceMaterial noMaterial    = {};
                BoundaryMediumPair noBoundary = {};
                uint directQuerySeed = PCGHash(rng.seed ^ PCGHash(depth + 0x9E3779B9u));
#if PT_VALIDATION
                PathContribution directContribution;
#endif
                float3 neeContribution;
                if (!SampleLd(scene, scatterPosition, -ray.Direction, noSurface, noMaterial, noBoundary,
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

                MediumPhaseSample phaseSample = {};
                bool isPhaseValid = SampleMediumPhase(scene.buffers, media, scatterPosition, ray.Direction, float3(constituentU, directionU), phaseSample); // Incoming-direction
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
                ray.TMax      = GetSceneRayLength(scene.buffers, ray.Origin, scene.sceneBoundsCenter, scene.sceneBoundsRadius);
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
                    scene, sphereHit, prevVertexPositionWS, lightPosition, ray.Direction,
                    depth, prevMarginalPDF, wasDelta);
            }
            else
            {
                float misWeight = (depth > 0u)
                    ? CalculateMISWeight(prevMarginalPDF, EnvironmentPDF(scene, ray.Direction), wasDelta)
                    : 1.0; // depth 0: camera ray looked straight at the sky
                emittedContribution = beta * EvaluateEnvironmentRadiance(scene, ray.Direction) * misWeight;
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
            toMedium.mediumID = instances[hp.instanceID].mediumID;
            toMedium.ior      = currentMedium.ior; // A pure boundary does not change IOR.

            if (!TryLoadMedium(scene.buffers, toMedium.mediumID, interiorData) ||
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
                float pdfLight  = AreaLightPDFAtHit(scene, prevVertexPositionWS, surfacePosition, ray.Direction);
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
                    scene.buffers,
                    scene.materialSlabs,
                    sm,
                    instances[hp.instanceID].mediumID,
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
        if (!SampleLd(scene, surfacePosition, woWS, hp, sm, boundaryPair,
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
            scene.buffers,
            scene.materialSlabs,
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
            RecordValidationWalkerAudit(scene.validationStats, PT_VALIDATION_WALKER_SAMPLE, sm.layerCount, sampleAudit);
#endif

        float marginalPDF = 0.0;
        if (s.valid != 0u && s.isDelta == 0u)
        {
#if PT_VALIDATION
            BxDF::MarginalPDFAudit marginalAudit;
#endif
            // Direction-space query used only by outer MIS
            marginalPDF = BxDF::DirectionalComposite::MarginalPDF(
                scene.buffers,
                scene.materialSlabs,
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
                    scene.buffers,
                    scene.materialSlabs,
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
                    scene.validationStats,
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
        if (depth >= 3u)
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
        ray.TMax      = GetSceneRayLength(scene.buffers, ray.Origin, scene.sceneBoundsCenter, scene.sceneBoundsRadius);
        if (ray.TMax <= ray.TMin)
            break;
        ++depth;
    }

    return L;
}

[shader("raygeneration")]
void RayGen()
{
    StructuredBuffer< SceneGlobals > sceneGlobals = GetResource(g_Push.ID_Scene);
    RWTexture2D< float4 >            accumulation = GetResource(g_Push.ID_Accumulation);
    RWTexture2D< float4 >            radiance     = GetResource(g_Push.ID_Radiance);

    const PathTracerConstants constants = g_Push.constants;

    PathScene scene;
    scene.buffers                       = sceneGlobals[0].buffers;
    scene.camera                        = sceneGlobals[0].camera;
    scene.maxDepth                      = constants.maxDepth;
    scene.sceneBoundsCenter             = constants.sceneBoundsCenter;
    scene.sceneBoundsRadius             = constants.sceneBoundsRadius;
    scene.primaryMediumSeed             = GetResource(g_Push.ID_PrimaryRayMediumSeed);
    scene.materialSlabs                 = GetResource(g_Push.ID_MaterialSlabs);
    scene.lightSelectionCDF             = GetResource(g_Push.ID_LightSelectionCDF);
    scene.environmentMap                = GetResource(g_Push.ID_EnvironmentMap);
    scene.environmentDistribution       = GetResource(g_Push.ID_EnvironmentDistribution);
    scene.environmentRadiance           = constants.environmentRadiance;
    scene.useEnvironmentMap             = constants.bUseEnvironmentMap;
    scene.useEnvironmentSampling        = constants.bUseEnvironmentSampling;
    scene.environmentDistributionWidth  = constants.environmentDistributionWidth;
    scene.environmentDistributionHeight = constants.environmentDistributionHeight;
#if PT_VALIDATION
    scene.validationStats               = GetResource(g_Push.ID_PathValidationStats);
#endif

    RayDesc ray;
    uint2 rayIndex      = DispatchRaysIndex().xy;
    uint2 rayDimensions = DispatchRaysDimensions().xy;

    uint   numSamples = max(constants.samplesPerFrame, 1u);
    float3 Lsum       = float3(0.0, 0.0, 0.0); // sum of this frame's path samples

#if PT_VALIDATION
    PathValidationSums validationSums = ZeroPathValidationSums();
#endif

    [loop]
    for (uint sampleOffset = 0u; sampleOffset < numSamples; ++sampleOffset)
    {
        uint sampleIndex = constants.accumulatedSampleCount + sampleOffset;
        RngState rng = InitSobolRng(rayIndex, constants.sobolSeed, sampleIndex);

        float2 pixelJitter = SamplePixelTent(NextFloat2(rng));
        float2 pathUV      = (float2(rayIndex) + 0.5 + pixelJitter) / float2(rayDimensions);
        float3 pathTarget  = ReconstructWorldPos(pathUV, 1.0, scene.camera.mViewProjInv);

        ray.Origin    = scene.camera.position;
        ray.Direction = normalize(pathTarget - scene.camera.position);
        ray.TMin      = scene.camera.zNear;
        // Physical transport uses the complete bounded scene
        ray.TMax      = GetSceneRayLength(scene.buffers, ray.Origin, scene.sceneBoundsCenter, scene.sceneBoundsRadius);
        if (ray.TMax <= ray.TMin)
            continue;

#if PT_VALIDATION
        SurfaceData primaryHit = {};

        PathContribution pathContribution;
        PathBSDFSample primaryBSDFSample = {};

        float3 sampleRadiance = TracePath(scene, ray, rng, 1u, primaryHit, pathContribution, primaryBSDFSample);
        if (IsPathFinite3(sampleRadiance))
            Lsum += sampleRadiance;

        AccumulateValidationContribution(validationSums, pathContribution);

        if (primaryHit.hitKind != 0u)
            validationSums.sampledLobeFrequency += BxDF::LayerComposite::SampledLobeVector(primaryBSDFSample);
#else
        float3 sampleRadiance = TracePath(scene, ray, rng);
        if (IsPathFinite3(sampleRadiance))
            Lsum += sampleRadiance;
#endif
    }

#if PT_VALIDATION
    PathValidationSums primaryValidation = TracePrimaryValidation(scene, rayIndex, rayDimensions);
#endif

    bool bReset = (constants.bResetAccumulation != 0u) || (constants.accumulatedSampleCount == 0u);

    // Accumulation
    float4 previousAccumulation  = bReset ? float4(0.0, 0.0, 0.0, 0.0) : accumulation[rayIndex];
    if (!IsPathFinite3(previousAccumulation.rgb) || !IsPathFinite(previousAccumulation.a))
        previousAccumulation = float4(0.0, 0.0, 0.0, 0.0);
    if (!IsPathFinite3(Lsum))
        Lsum = float3(0.0, 0.0, 0.0);

    float3 accumulatedRadiance   = previousAccumulation.rgb + Lsum;
    float  accumulatedSamples    = previousAccumulation.a + float(numSamples);
    float  invAccumulatedSamples = 1.0 / accumulatedSamples;

    accumulation[rayIndex] = float4(accumulatedRadiance, accumulatedSamples);
    radiance[rayIndex]     = float4(accumulatedRadiance * invAccumulatedSamples, 1.0);

#if PT_VALIDATION
    PathValidationTargets targets;
    targets.albedo                = GetResource(g_Push.ID_Albedo);
    targets.normal                = GetResource(g_Push.ID_Normal);
    targets.depth                 = GetResource(g_Push.ID_Depth);
    targets.geometricNormal       = GetResource(g_Push.ID_GeometricNormal);
    targets.materialParams        = GetResource(g_Push.ID_MaterialParams);
    targets.materialExtra         = GetResource(g_Push.ID_MaterialExtra);
    targets.materialSpecularColor = GetResource(g_Push.ID_MaterialSpecularColor);
    targets.emission              = GetResource(g_Push.ID_Emission);
    targets.diffuseRadiance       = GetResource(g_Push.ID_DiffuseRadiance);
    targets.specularRadiance      = GetResource(g_Push.ID_SpecularRadiance);
    targets.transmissionRadiance  = GetResource(g_Push.ID_TransmissionRadiance);
    targets.surfaceLobeMask       = GetResource(g_Push.ID_SurfaceLobeMask);
    targets.surfaceLobeWeight     = GetResource(g_Push.ID_SurfaceLobeWeight);
    targets.sampledLobeFrequency  = GetResource(g_Push.ID_SampledLobeFrequency);
    targets.primaryId             = GetResource(g_Push.ID_PrimaryId);

    WritePathValidationAOVs(targets, rayIndex, validationSums, primaryValidation, bReset, previousAccumulation.a, invAccumulatedSamples);
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

bool AlphaTestRejectsHit(SceneBufferIndices buffers, BuiltInTriangleIntersectionAttributes attr, bool bSampleBlend, uint seed)
{
    StructuredBuffer< MeshData >            meshes           = GetResource(buffers.meshes);
    StructuredBuffer< InstanceData >        instances        = GetResource(buffers.instances);
    StructuredBuffer< uint >                indices          = GetResource(buffers.indices);
    StructuredBuffer< Vertex >              vertices         = GetResource(buffers.vertices);
    StructuredBuffer< MaterialData >        materials        = GetResource(buffers.materials);
    StructuredBuffer< MaterialTextureData > materialTextures = GetResource(buffers.materialTextures);

    InstanceData instance = instances[InstanceID()];
    if (instance.materialID == kInvalidIndex)
        return false;

    MaterialData mat = materials[instance.materialID];

    bool isMask  = (mat.materialFlags & MATERIAL_FLAG_ALPHA_MASK) != 0u;
    bool isBlend = (mat.materialFlags & MATERIAL_FLAG_ALPHA_BLEND) != 0u;
    if (!isMask && !(bSampleBlend && isBlend))
        return false;

    MeshData mesh = meshes[instance.meshID];
    uint primIdx = PrimitiveIndex();
    uint indexBase = mesh.lods[0].iOffset + primIdx * 3u;
    uint i0 = indices[indexBase + 0u];
    uint i1 = indices[indexBase + 1u];
    uint i2 = indices[indexBase + 2u];

    Vertex v0 = vertices[mesh.vOffset + i0];
    Vertex v1 = vertices[mesh.vOffset + i1];
    Vertex v2 = vertices[mesh.vOffset + i2];

    float3 bary = float3(
        1.0 - attr.barycentrics.x - attr.barycentrics.y,
        attr.barycentrics.x,
        attr.barycentrics.y);

    float2 uv =
        v0.uv * bary.x +
        v1.uv * bary.y +
        v2.uv * bary.z;

    float opacity = ReadOpacityLevel(materialTextures, mat, uv);
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
    StructuredBuffer< SceneGlobals > sceneGlobals = GetResource(g_Push.ID_Scene);
    if (AlphaTestRejectsHit(sceneGlobals[0].buffers, attr, false, 0u))
        IgnoreHit();
}

[shader("anyhit")]
void AnyHit_ShadowAlpha(inout ShadowPayload sp, in BuiltInTriangleIntersectionAttributes attr)
{
    StructuredBuffer< SceneGlobals > sceneGlobals = GetResource(g_Push.ID_Scene);
    if (AlphaTestRejectsHit(sceneGlobals[0].buffers, attr, true, sp.alphaSeed))
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
