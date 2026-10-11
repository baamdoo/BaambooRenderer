#ifndef _HLSL_PATHSAMPLING_HEADER
#define _HLSL_PATHSAMPLING_HEADER

#include "Sampling.hlsli"
#include "PathComposite.hlsli"

float2 EnvironmentUVFromDirection(float3 directionWS)
{
    float3 d = normalize(directionWS);
    
    float u = atan2(d.x, -d.z) / (2.0 * PI); u = frac(u + 1.0);
    float v = atan2(length(d.xz), d.y) / PI;
    return float2(u, v);
}

float3 EnvironmentDirectionFromUV(float2 uv)
{
    float phi   = uv.x * 2.0 * PI;
    float theta = uv.y * PI;
    
    float sinTheta = sin(theta);
    return float3(sinTheta * sin(phi), cos(theta), -sinTheta * cos(phi));
}

float3 EvaluateEnvironmentRadiance(float3 directionWS)
{
    if (g_UseEnvironmentMap == 0u)
        return g_EnvironmentRadiance;

    Texture2D< float4 > EnvMap = GetResource(g_EnvironmentMap.index);
    uint width, height;
    EnvMap.GetDimensions(width, height);
    float2 uv = EnvironmentUVFromDirection(directionWS);
    
    float halfTexelV = 0.5 / float(height);
    // Longitude is periodic so use wrap sampler for uv.x
    uv.y = clamp(uv.y, halfTexelV, 1.0 - halfTexelV);
    return max(EnvMap.SampleLevel(g_LinearWrapSampler, uv, 0).rgb, float3(0.0, 0.0, 0.0));
}

// Environment importance sampling
bool HasEnvironmentDistribution()
{
    return g_UseEnvironmentMap != 0u && g_UseEnvironmentSampling != 0u &&
        g_EnvironmentDistributionWidth > 0u && g_EnvironmentDistributionHeight > 0u;
}

// Inverse-transform sampling
uint EnvironmentCDFLowerBound(uint baseIndex, uint count, float u)
{
    StructuredBuffer< float > EnvDistribution = GetResource(g_EnvironmentDistribution.index);

    float target = clamp(u, 0.0, 0.99999994);

    uint lo = 0u, hi = count;
    [loop]
    while (lo < hi)
    {
        uint mid = (lo + hi) >> 1u;

        float distribution = EnvDistribution[baseIndex + mid];
        if (distribution <= target)
            lo = mid + 1;
        else
            hi = mid;
    }
    
    return min(lo, count - 1u);
}

float EnvironmentCDFSegmentProbability(uint baseIndex, uint index)
{
    StructuredBuffer<float> EnvDistribution = GetResource(g_EnvironmentDistribution.index);
    
    float cdf1 = EnvDistribution[baseIndex + index];
    float cdf0 = (index > 0u) ? EnvDistribution[baseIndex + index - 1u] : 0.0;
    return max(cdf1 - cdf0, 0.0);
}

float EnvironmentPDF(float3 directionW)
{
    if (!HasEnvironmentDistribution())
        return 0.0;

    uint width  = g_EnvironmentDistributionWidth;
    uint height = g_EnvironmentDistributionHeight;

    float sinTheta = length(normalize(directionW).xz);
    if (!isfinite(sinTheta) || sinTheta == 0.0)
        return 0.0;
    
    float2 uv = EnvironmentUVFromDirection(directionW);
    uint x = min((uint)(uv.x * (float)width), width - 1u);
    uint y = min((uint)(uv.y * (float)height), height - 1u);

    float rowProb = EnvironmentCDFSegmentProbability(0u, y);
    float colProb = EnvironmentCDFSegmentProbability(height + y * width, x);
    
    float jacobian = 2.0 * PI * PI * sinTheta;
    return (rowProb * colProb) * width * height / jacobian;
}

bool SampleEnvironmentLight(float4 u, out float3 wiWS, out float3 Lenv, out float pdfW)
{
    wiWS = float3(0.0, 1.0, 0.0);
    Lenv = float3(0.0, 0.0, 0.0);
    pdfW = 0.0;
    if (!HasEnvironmentDistribution())
        return false;

    uint width  = g_EnvironmentDistributionWidth;
    uint height = g_EnvironmentDistributionHeight;
    uint y = EnvironmentCDFLowerBound(0u, height, u.x);
    uint x = EnvironmentCDFLowerBound(height + y * width, width, u.y);
    
    float2 uv = (float2((float)x, (float)y) + clamp(u.zw, 0.0, 0.99999994)) / float2((float)width, (float)height);
    wiWS = normalize(EnvironmentDirectionFromUV(uv));
    
    pdfW = EnvironmentPDF(wiWS);
    if (!isfinite(pdfW) || pdfW <= 0.0)
        return false;
    
    Lenv = EvaluateEnvironmentRadiance(wiWS);
    return any(Lenv > 0.0);
}


float3 AreaLightRadiance(AreaLight light)
{
    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    float  area       = max(4.0 * light.halfWidth * light.halfHeight, EPSILON_MIN);
    return lightColor * light.luminousFluxLm / (PI * area);
}

float3 DiskLightRadiance(DiskLight light)
{
    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    float  area       = max(PI * light.radius * light.radius, EPSILON_MIN);
    return lightColor * light.luminousFluxLm / (PI * area);
}

float3 SphereLightRadiance(SphereLight light)
{
    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    float  area       = max(4.0 * PI * light.radius * light.radius, EPSILON_MIN);
    return lightColor * light.luminousFluxLm / (PI * area);
}

float3 TubeLightRadiance(TubeLight light)
{
    float3 a = float3(light.posAX, light.posAY, light.posAZ);
    float3 b = float3(light.posBX, light.posBY, light.posBZ);
    float  lengthTube = length(b - a);
    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    float  area = max(2.0 * PI * light.radius * lengthTube, EPSILON_MIN);
    return lightColor * light.luminousFluxLm / (PI * area);
}

float3 DirectionalLightIrradiance(DirectionalLight light)
{
    return float3(light.colorR, light.colorG, light.colorB) * light.illuminanceLux;
}

float3 SpotLightIntensity(SpotLight light)
{
    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    float  cosOuter   = cos(light.outerConeAngleRad);
    float  solidAngle = 2.0 * PI * max(1.0 - cosOuter, EPSILON_MIN);
    return lightColor * light.luminousFluxLm / solidAngle;
}

float SpotLightFalloff(SpotLight light, float3 wiW)
{
    if (light.outerConeAngleRad <= 0.0)
        return 0.0;

    float3 lightDir = normalize(float3(light.dirX, light.dirY, light.dirZ));
    float  innerAngle = min(light.innerConeAngleRad, light.outerConeAngleRad);
    float  cosTheta = dot(wiW, -lightDir);
    float  cosOuter = cos(light.outerConeAngleRad);
    float  cosInner = cos(innerAngle);

    if (cosInner <= cosOuter + 1.0e-5)
        return cosTheta >= cosOuter ? 1.0 : 0.0;

    return smoothstep(cosOuter, cosInner, cosTheta);
}

uint DirectLightCount()
{
    return g_Lights.numDirectionals + g_Lights.numSpots + g_Lights.numAreas + g_Lights.numDisks + g_Lights.numSpheres + g_Lights.numTubes;
}

// Inverse-transform sampling
uint LightSelectionCDFLowerBound(uint count, float u)
{
    StructuredBuffer< float > LightCDF = GetResource(g_LightSelectionCDF.index);

    float target = clamp(u, 0.0, 0.99999994);

    uint lo = 0u, hi = count;
    [loop]
    while (lo < hi)
    {
        uint mid = (lo + hi) >> 1u;
        if (LightCDF[mid] < target)
            lo = mid + 1;
        else
            hi = mid;
    }

    return min(lo, count - 1u);
}

float LightSelectionPMF(uint lightIndex)
{
    StructuredBuffer< float > LightCDF = GetResource(g_LightSelectionCDF.index);

    float cdf1 = LightCDF[lightIndex];
    float cdf0 = (lightIndex > 0u) ? LightCDF[lightIndex - 1u] : 0.0;
    return max(cdf1 - cdf0, 0.0);
}

float PdfAreaToSolidAngle(float pdfA, float distSq, float cosLight)
{
    float pdfW = pdfA * distSq / max(cosLight, EPSILON_MIN);
    return pdfW;
}


// Single NEE light sample
struct LightSample
{
    float3 wiWS;         // x -> light direction (world), normalized
    float3 shadowTarget; // light point y for the shadow ray (point/area types)
    float3 Le;           // area types: emitted radiance; delta types: intensity term
    float  deltaScale;   // delta extras (spot falloff*attenuation); 1 for directional
    float  pdfW;         // selection-weighted solid-angle pdf (area types); 0 for delta
    float  selectionPdf; // this light's selection PMF (power CDF segment)

    uint isDelta;       // 1 -> Dirac pdf: no MIS competition (directional/spot)
    uint isDirectional; // 1 -> use a scene-bound endpoint along wiWS for transport
    uint valid;
};

LightSample SampleOneLight(float3 p, inout RngState rng)
{
    LightSample ls = (LightSample)0;

    uint lightCount = DirectLightCount();
    uint lightIndex = LightSelectionCDFLowerBound(lightCount, NextFloat(rng));
    float selectionPdf = LightSelectionPMF(lightIndex);
    if (selectionPdf <= 0.0)
        return ls;
    ls.selectionPdf = selectionPdf;

    if (lightIndex < g_Lights.numDirectionals)
    {
        DirectionalLight light = g_Lights.directionals[lightIndex];
        ls.wiWS          = normalize(-float3(light.dirX, light.dirY, light.dirZ));
        ls.Le            = DirectionalLightIrradiance(light);
        ls.deltaScale    = 1.0;
        ls.isDelta       = 1u;
        ls.isDirectional = 1u;
        ls.valid         = 1u;
        return ls;
    }
    lightIndex -= g_Lights.numDirectionals;

    if (lightIndex < g_Lights.numSpots)
    {
        SpotLight light = g_Lights.spots[lightIndex];
        float3 lightPos = float3(light.posX, light.posY, light.posZ);
        float3 toLight  = lightPos - p;
        float  dist2    = dot(toLight, toLight);
        if (dist2 <= EPSILON_MIN)
            return ls;

        float dist = sqrt(dist2);
        ls.wiWS = toLight / dist;

        float falloff = SpotLightFalloff(light, ls.wiWS);
        if (falloff <= 0.0)
            return ls;

        float radius2     = light.radiusM * light.radiusM;
        float attenuation = 1.0 / max(dist2, radius2);

        ls.shadowTarget = lightPos;
        ls.Le           = SpotLightIntensity(light);
        ls.deltaScale   = falloff * attenuation;
        ls.isDelta      = 1u;
        ls.valid        = 1u;
        return ls;
    }
    lightIndex -= g_Lights.numSpots;

    if (lightIndex < g_Lights.numAreas)
    {
        AreaLight light = g_Lights.areas[lightIndex];

        float3 y;
        float3 lightBackNormal;
        float  pdfA;
        SampleAreaLight(NextFloat2(rng), light, y, lightBackNormal, pdfA);

        float3 toLight = y - p;
        float  dist2   = dot(toLight, toLight);
        if (dist2 <= EPSILON_MIN || pdfA <= 0.0)
            return ls;

        float dist = sqrt(dist2);
        ls.wiWS = toLight / dist;

        float cosLight = saturate(dot(-lightBackNormal, -ls.wiWS));
        if (cosLight <= 0.0)
            return ls;

        ls.shadowTarget = y;
        ls.Le           = AreaLightRadiance(light);
        ls.pdfW         = PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
        ls.valid        = 1u;
        return ls;
    }
    lightIndex -= g_Lights.numAreas;

    if (lightIndex < g_Lights.numDisks)
    {
        DiskLight light = g_Lights.disks[lightIndex];

        float3 y;
        float3 lightBackNormal;
        float  pdfA;
        SampleDiskLight(NextFloat2(rng), light, y, lightBackNormal, pdfA);

        float3 toLight = y - p;
        float  dist2   = dot(toLight, toLight);
        if (dist2 <= EPSILON_MIN || pdfA <= 0.0)
            return ls;

        float dist = sqrt(dist2);
        ls.wiWS = toLight / dist;

        float cosLight = saturate(dot(-lightBackNormal, -ls.wiWS));
        if (cosLight <= 0.0)
            return ls;

        ls.shadowTarget = y;
        ls.Le           = DiskLightRadiance(light);
        ls.pdfW         = PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
        ls.valid        = 1u;
        return ls;
    }
    lightIndex -= g_Lights.numDisks;

    if (lightIndex < g_Lights.numSpheres)
    {
        SphereLight light = g_Lights.spheres[lightIndex];
        if (!isfinite(light.radius) || light.radius <= 0.0 ||
            !all(isfinite(float3(light.posX, light.posY, light.posZ))))
            return ls;

        float3 y;
        float3 lightNormal;
        float  pdfA;
        SampleSphereLight(NextFloat2(rng), light, y, lightNormal, pdfA);

        float3 toLight = y - p;
        float  dist2   = dot(toLight, toLight);
        if (dist2 <= EPSILON_MIN || !isfinite(pdfA) || pdfA <= 0.0)
            return ls;

        float dist = sqrt(dist2);
        ls.wiWS = toLight / dist;

        float cosLight = saturate(dot(lightNormal, -ls.wiWS));
        if (cosLight <= 0.0)
            return ls;

        ls.shadowTarget = y;
        ls.Le           = SphereLightRadiance(light);
        ls.pdfW         = PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
        ls.valid        = 1u;
        return ls;
    }
    lightIndex -= g_Lights.numSpheres;

    if (lightIndex < g_Lights.numTubes)
    {
        TubeLight light = g_Lights.tubes[lightIndex];

        float3 y;
        float3 lightNormal;
        float  pdfA;
        SampleTubeLight(NextFloat2(rng), light, y, lightNormal, pdfA);

        float3 toLight = y - p;
        float  dist2   = dot(toLight, toLight);
        if (dist2 <= EPSILON_MIN || pdfA <= 0.0)
            return ls;

        float dist = sqrt(dist2);
        ls.wiWS = toLight / dist;

        float cosLight = saturate(dot(lightNormal, -ls.wiWS));
        if (cosLight <= 0.0)
            return ls;

        ls.shadowTarget = y;
        ls.Le           = TubeLightRadiance(light);
        ls.pdfW         = PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
        ls.valid        = 1u;
        return ls;
    }

    return ls;
}

// Exact analytic endpoint identity, unlike the positional mesh-emitter matcher.
float SphereLightPDFAtHit(uint sphereIndex, float3 refP, float3 hitP, float3 normal, float3 wiWS)
{
    SphereLight light = g_Lights.spheres[sphereIndex];
    float cosLight = saturate(dot(normal, -wiWS));
    float3 toLight = hitP - refP;
    float dist2 = dot(toLight, toLight);
    // Match SampleOneLight's near-field rejection: no competing NEE density
    // exists there, so the BSDF-hit branch must retain unit MIS weight.
    if (!isfinite(dist2) || dist2 <= EPSILON_MIN || !isfinite(cosLight) || cosLight <= 0.0 || light.radius <= 0.0)
        return 0.0;
    uint sphereBase = g_Lights.numDirectionals + g_Lights.numSpots + g_Lights.numAreas + g_Lights.numDisks;
    float selectionPdf = LightSelectionPMF(sphereBase + sphereIndex);
    float pdfA = 1.0 / (4.0 * PI * light.radius * light.radius); // Same density as SampleSphereLight.
    if (!isfinite(pdfA) || pdfA <= 0.0)
        return 0.0;
    return PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
}

float3 EvaluateAnalyticSphereEmission(
    AnalyticSphereLightHit hit, float3 refP, float3 hitP, float3 wiWS,
    uint depth, float scatterPDF, uint wasDelta)
{
    if (dot(hit.normal, -wiWS) <= 0.0)
        return 0.0;

    SphereLight endpoint = g_Lights.spheres[hit.sphereIndex];
    float3 result = 0.0;
    [loop]
    for (uint i = 0u; i < g_Lights.numSpheres; ++i)
    {
        SphereLight light = g_Lights.spheres[i];
        // Exactly coincident LightComponents remain separate emission terms.
        // Each has its own unchanged NEE selection PMF and complementary MIS
        // weight; they share one opaque endpoint, not two path vertices.
        if (light.posX != endpoint.posX || light.posY != endpoint.posY ||
            light.posZ != endpoint.posZ || light.radius != endpoint.radius)
            continue;
        float pdfLight = SphereLightPDFAtHit(i, refP, hitP, hit.normal, wiWS);
        float misWeight = depth == 0u ? 1.0 : CalculateMISWeight(scatterPDF, pdfLight, wasDelta);
        result += SphereLightRadiance(light) * misWeight;
    }
    return result;
}

// Direct lighting at either a surface or a volume scattering vertex
bool SampleLd(
    float3 p,
    float3 woWS,
    SurfaceData surface,
    SurfaceMaterial material,
    BoundaryMediumPair boundaryPair,
    MediumStack media,
    MediumStack hostStack,
    MediumStack airStack,
    bool canChangePathMedium,
    uint depth,
    uint directionalQuerySeed,
    bool bUseMIS,
    inout RngState rng,
    out float3 direct
#if PT_VALIDATION
    , uint recordValidationStats
    , out PathContribution contribution
#endif
)
{
    direct = 0.0;
#if PT_VALIDATION
    contribution = ZeroPathContribution();
#endif
    const bool bSurface = surface.hitKind != 0u;
    BxDF::Frame frame = (BxDF::Frame)0;
    float3 wo = 0.0;
    bool bHasTransmission = false;
    if (bSurface)
    {
        frame = MakeSurfaceFrame(surface);
        wo = BxDF::ToLocal(frame, woWS);
        bHasTransmission = HasTransmissionLobe(material);
        if ((!bHasTransmission && wo.z <= 0.0) || (bHasTransmission && wo.z == 0.0) ||
            BxDF::LayerComposite::IsSmoothConductor(material))
            return true;
    }

    // one local light and one environment direction
    [loop]
    for (uint source = 0u; source < 2u; ++source)
    {
        const bool bEnvironment = source == 1u;
        LightSample ls = (LightSample)0;
        if (bEnvironment)
        {
            if (!HasEnvironmentDistribution())
                continue;
            SobolRebaseForBounce(rng, depth, SOBOL_SECTION_NEE_ENV);
            float2 cellU = NextFloat2(rng);
            float2 directionU = NextFloat2(rng);
            if (!SampleEnvironmentLight(float4(cellU, directionU), ls.wiWS, ls.Le, ls.pdfW))
                continue;
        }
        else
        {
            if (DirectLightCount() == 0u)
                continue;
            SobolRebaseForBounce(rng, depth, SOBOL_SECTION_NEE_LIGHT);
            ls = SampleOneLight(p, rng);
            if (ls.valid == 0u)
                continue;
        }

        float3 wi = 0.0;
        float cosSurface = 1.0;
        float3 shadowOrigin = p;
        MediumStack shadowStack = hostStack;
        if (bSurface)
        {
            wi = BxDF::ToLocal(frame, ls.wiWS);
            cosSurface = bHasTransmission ? BxDF::AbsCosTheta(wi) : saturate(wi.z);
            if (cosSurface <= 0.0)
                continue;
        }

        uint alphaSeed = NextUint(rng);
        if (bSurface)
        {
            Medium shadowStartMedium;
            if (!BxDF::DirectionalComposite::TryResolveShadowStartMedium(
                    surface.geometricNormal, woWS, ls.wiWS, wo, wi, boundaryPair, shadowStartMedium))
                continue;
            // crossing changes only this connection's host copy; it does not change the main path.
            if (!TryPrepareShadowMediumStack(hostStack, surface.instanceID, boundaryPair,
                    canChangePathMedium, surface.geometricNormal, woWS, ls.wiWS, shadowStack))
                return false;
            shadowOrigin = OffsetRay(p, surface.geometricNormal, ls.wiWS);
        }

        float3 shadowTarget = ls.shadowTarget;
        if (bEnvironment || ls.isDirectional != 0u)
        {
            float distance = GetSceneRayLength(shadowOrigin, g_SceneBoundsCenter, g_SceneBoundsRadius);
            if (distance <= 0.0)
                return false;
            shadowTarget = shadowOrigin + distance * ls.wiWS;
        }

        uint shadowSalt = bEnvironment ? 0x45534844u : 0x53534844u;
        RngState transmittanceRng = InitRng(uint2(0, 0), PCGHash(directionalQuerySeed ^ alphaSeed ^ shadowSalt), 0u);
        float3 transmittance;
        if (!EstimateShadowTransmittance(shadowOrigin, shadowTarget, shadowStack, airStack,
                alphaSeed, transmittanceRng, transmittance))
            return false;
        if (!any(transmittance > 0.0))
            continue;

        float3 response;
        float scatterPDF = 0.0;
#if PT_VALIDATION
        PathContribution bsdfLobes = ZeroPathContribution();
#endif
        if (bSurface)
        {
#if PT_VALIDATION
            BxDF::LayerWalkerAudit evaluateAudit;
#endif
            response = BxDF::DirectionalComposite::Evaluate(
                material, surface.uv, surface.ddxUV, surface.ddyUV, wo, wi,
                boundaryPair, directionalQuerySeed
#if PT_VALIDATION
                , evaluateAudit
#endif
            );
#if PT_VALIDATION
            if (recordValidationStats != 0u)
                RecordValidationWalkerAudit(PT_VALIDATION_WALKER_EVALUATE, material.layerCount, evaluateAudit);
#endif
            if (!any(response > 0.0))
                continue;
            response *= cosSurface;

#if PT_VALIDATION
            if (material.layerCount <= 1u)
            {
                float iorExterior = boundaryPair.isEntering != 0u ? boundaryPair.mediumI.ior : boundaryPair.mediumT.ior;
                float incidentFrameSign = BxDF::LayerComposite::GetIncidentFrameSign(wo, boundaryPair.isEntering);
                bsdfLobes = BxDF::LayerComposite::EvaluateBoundaryLobes(material,
                    incidentFrameSign * wo, incidentFrameSign * wi, iorExterior, material.ior);
            }
#endif
            if (bUseMIS && ls.isDelta == 0u)
            {
#if PT_VALIDATION
                BxDF::MarginalPDFAudit marginalAudit;
#endif
                scatterPDF = BxDF::DirectionalComposite::MarginalPDF(
                    material, surface.uv, surface.ddxUV, surface.ddyUV, wo, wi,
                    boundaryPair, directionalQuerySeed
#if PT_VALIDATION
                    , marginalAudit
#endif
                );
#if PT_VALIDATION
                if (recordValidationStats != 0u)
                {
                    uint retryClassification = DiagnoseValidationMarginalPDFZero(
                        material, surface.uv, surface.ddxUV, surface.ddyUV, wo, wi,
                        boundaryPair, directionalQuerySeed, marginalAudit);
                    RecordValidationMarginalPDF(
                        bEnvironment ? PT_VALIDATION_QUERY_ENVIRONMENT_NEE : PT_VALIDATION_QUERY_FINITE_NEE,
                        material.layerCount, scatterPDF, marginalAudit, retryClassification);
                }
#endif
            }
        }
        else
        {
            // HG helpers use the incoming travel direction
            if (!EvaluateMediumPhase(media, p, dot(ls.wiWS, -woWS), response, scatterPDF))
                return false;
        }

        float3 lightScale;
        if (ls.isDelta != 0u)
        {
            if (ls.selectionPdf <= 0.0)
                return false;
            lightScale = ls.Le * ls.deltaScale / ls.selectionPdf;
        }
        else
        {
            if (ls.pdfW <= 0.0)
                return false;
            float misWeight = bUseMIS ? PowerHeuristic(ls.pdfW, scatterPDF) : 1.0;
            lightScale = misWeight * ls.Le / ls.pdfW;
        }
        lightScale *= transmittance;
        direct += response * lightScale;
#if PT_VALIDATION
        contribution.diffuse      += bsdfLobes.diffuse * cosSurface * lightScale;
        contribution.specular     += bsdfLobes.specular * cosSurface * lightScale;
        contribution.transmission += bsdfLobes.transmission * cosSurface * lightScale;
#endif
    }
    return true;
}


float AreaLightPDFAtHit(float3 refP, float3 hitP, float3 wiWS)
{
    float pdfW = 0.0;
    if (DirectLightCount() == 0u)
        return pdfW;

    // Per-light selection PMF (power CDF)
    uint areaBase   = g_Lights.numDirectionals + g_Lights.numSpots;
    uint diskBase   = areaBase + g_Lights.numAreas;
    uint sphereBase = diskBase + g_Lights.numDisks;
    uint tubeBase   = sphereBase + g_Lights.numSpheres;

    [loop]
    for (uint i = 0u; i < g_Lights.numAreas; ++i)
    {
        AreaLight light = g_Lights.areas[i];
        float3 position  = float3(light.posX,     light.posY,     light.posZ);
        float3 tangent   = normalize(float3(light.tangentX, light.tangentY, light.tangentZ));
        float3 normal    = normalize(float3(light.normalX,  light.normalY,  light.normalZ));
        float3 bitangent = cross(tangent, normal);
        float3 local     = hitP - position;

        if (abs(dot(local, normal)) > 1.0e-3)
            continue;

        float u = dot(local, tangent);
        float v = dot(local, bitangent);
        if (abs(u) > light.halfWidth + 1.0e-3 || abs(v) > light.halfHeight + 1.0e-3)
            continue;

        float cosLight = saturate(dot(-normal, -wiWS));
        if (cosLight <= 0.0)
            continue;

        float3 toLight = hitP - refP;
        float dist2 = dot(toLight, toLight);
        float pdfA = 1.0 / max(4.0 * light.halfWidth * light.halfHeight, EPSILON_MIN);
        float selectionPdf = LightSelectionPMF(areaBase + i);
        pdfW += PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
    }

    [loop]
    for (uint i = 0u; i < g_Lights.numDisks; ++i)
    {
        DiskLight light = g_Lights.disks[i];
        float3 position  = float3(light.posX,     light.posY,     light.posZ);
        float3 tangent   = normalize(float3(light.tangentX, light.tangentY, light.tangentZ));
        float3 normal    = normalize(float3(light.normalX,  light.normalY,  light.normalZ));
        float3 bitangent = cross(tangent, normal);
        float3 local     = hitP - position;

        if (abs(dot(local, normal)) > 1.0e-3)
            continue;

        float u = dot(local, tangent);
        float v = dot(local, bitangent);
        if (u * u + v * v > light.radius * light.radius + 1.0e-3)
            continue;

        float cosLight = saturate(dot(-normal, -wiWS));
        if (cosLight <= 0.0)
            continue;

        float3 toLight = hitP - refP;
        float dist2 = dot(toLight, toLight);
        float pdfA = 1.0 / max(PI * light.radius * light.radius, EPSILON_MIN);
        float selectionPdf = LightSelectionPMF(diskBase + i);
        pdfW += PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
    }

    [loop]
    for (uint i = 0u; i < g_Lights.numSpheres; ++i)
    {
        SphereLight light = g_Lights.spheres[i];
        float3 center = float3(light.posX, light.posY, light.posZ);
        float3 local = hitP - center;
        float distToCenter = length(local);
        if (abs(distToCenter - light.radius) > 1.0e-3)
            continue;

        float3 normal = local / max(distToCenter, EPSILON_MIN);
        float cosLight = saturate(dot(normal, -wiWS));
        if (cosLight <= 0.0)
            continue;

        float3 toLight = hitP - refP;
        float dist2 = dot(toLight, toLight);
        float pdfA = 1.0 / max(4.0 * PI * light.radius * light.radius, EPSILON_MIN);
        float selectionPdf = LightSelectionPMF(sphereBase + i);
        pdfW += PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
    }

    [loop]
    for (uint i = 0u; i < g_Lights.numTubes; ++i)
    {
        TubeLight light = g_Lights.tubes[i];
        float3 a = float3(light.posAX, light.posAY, light.posAZ);
        float3 b = float3(light.posBX, light.posBY, light.posBZ);
        float3 axisVec = b - a;
        float  lengthTube = length(axisVec);
        if (lengthTube <= EPSILON_MIN || light.radius <= 0.0)
            continue;

        float3 axis = axisVec / lengthTube;
        float3 local = hitP - a;
        float  axial = dot(local, axis);
        if (axial < -1.0e-3 || axial > lengthTube + 1.0e-3)
            continue;

        float3 radial = local - axis * axial;
        float  radialLen = length(radial);
        if (abs(radialLen - light.radius) > 1.0e-3)
            continue;

        float3 normal = radial / max(radialLen, EPSILON_MIN);
        float cosLight = saturate(dot(normal, -wiWS));
        if (cosLight <= 0.0)
            continue;

        float3 toLight = hitP - refP;
        float dist2 = dot(toLight, toLight);
        float pdfA = 1.0 / max(2.0 * PI * light.radius * lengthTube, EPSILON_MIN);
        float selectionPdf = LightSelectionPMF(tubeBase + i);
        pdfW += PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
    }

    return pdfW;
}


#endif // _HLSL_PATHSAMPLING_HEADER
