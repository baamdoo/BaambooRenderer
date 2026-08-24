#ifndef _HLSL_PATHVALIDATION_HEADER
#define _HLSL_PATHVALIDATION_HEADER

#if PT_VALIDATION
static const uint PT_VALIDATION_BSDF_DIFFUSE           = 1u;
static const uint PT_VALIDATION_BSDF_CONDUCTOR         = 2u;
static const uint PT_VALIDATION_BSDF_DIELECTRIC        = 3u;
static const uint PT_VALIDATION_BSDF_MIXED             = 4u;
static const uint PT_VALIDATION_BSDF_PRINCIPLED        = 5u;
static const uint PT_VALIDATION_BSDF_MIXED_DIELECTRIC  = 6u;
static const uint PT_VALIDATION_BSDF_OPAQUE_DIELECTRIC = 7u;

uint ClassifyValidationBSDF(SurfaceMaterial material)
{
    if (IsPrincipledMaterial(material))
        return PT_VALIDATION_BSDF_PRINCIPLED;

    float transmission = saturate(material.transmission);
    float opaque       = 1.0 - transmission;
    float metallic     = saturate(material.metallic);

    bool hasTransmission       = transmission > PT_LOBE_EPS;
    bool hasDiffuse            = opaque * (1.0 - metallic) > PT_LOBE_EPS;
    bool hasMetallicSpecular   = opaque * metallic > PT_LOBE_EPS;
    bool hasDielectricSpecular = HasDielectricSpecularLobe(material, max(material.ior, 1.0e-4));

    if (hasTransmission && (hasDiffuse || hasMetallicSpecular))
        return PT_VALIDATION_BSDF_MIXED_DIELECTRIC;
    if (hasTransmission)
        return PT_VALIDATION_BSDF_DIELECTRIC;
    if (hasDiffuse && hasMetallicSpecular)
        return PT_VALIDATION_BSDF_MIXED;
    if (hasMetallicSpecular)
        return PT_VALIDATION_BSDF_CONDUCTOR;
    if (hasDiffuse && hasDielectricSpecular)
        return PT_VALIDATION_BSDF_OPAQUE_DIELECTRIC;
    return PT_VALIDATION_BSDF_DIFFUSE;
}


static const uint PT_VALIDATION_STAT_CONTINUATION_TOTAL          = 0u;
static const uint PT_VALIDATION_STAT_CONTINUATION_ZERO           = 1u;
static const uint PT_VALIDATION_STAT_CONTINUATION_NEAR_ZERO      = 2u;
static const uint PT_VALIDATION_STAT_FINITE_NEE_TOTAL            = 3u;
static const uint PT_VALIDATION_STAT_FINITE_NEE_ZERO             = 4u;
static const uint PT_VALIDATION_STAT_FINITE_NEE_NEAR_ZERO        = 5u;
static const uint PT_VALIDATION_STAT_ENVIRONMENT_NEE_TOTAL       = 6u;
static const uint PT_VALIDATION_STAT_ENVIRONMENT_NEE_ZERO        = 7u;
static const uint PT_VALIDATION_STAT_ENVIRONMENT_NEE_NEAR_ZERO   = 8u;
static const uint PT_VALIDATION_STAT_NO_CONTINUOUS_PROPOSAL      = 9u;
static const uint PT_VALIDATION_STAT_SINGLE_LAYER_ZERO           = 10u;
static const uint PT_VALIDATION_STAT_NLAYER_ZERO_CANDIDATE       = 11u;
static const uint PT_VALIDATION_STAT_NLAYER_RETRY_ANY_POSITIVE   = 12u;
static const uint PT_VALIDATION_STAT_NLAYER_RETRY_ALL_ZERO       = 13u;
static const uint PT_VALIDATION_STAT_NONFINITE                   = 14u;
static const uint PT_VALIDATION_STAT_NLAYER_POSITIVE             = 15u;
static const uint PT_VALIDATION_STAT_SAMPLE_HISTORY_BASE         = 16u;
static const uint PT_VALIDATION_STAT_EVALUATE_HISTORY_BASE       = 23u;
static const uint PT_VALIDATION_STAT_PDF_HISTORY_BASE            = 30u;
static const uint PT_VALIDATION_STAT_SAMPLE_FORWARD_SUM          = 37u;
static const uint PT_VALIDATION_STAT_EVALUATE_FORWARD_SUM        = 38u;
static const uint PT_VALIDATION_STAT_EVALUATE_REVERSE_CONT_SUM   = 39u;
static const uint PT_VALIDATION_STAT_EVALUATE_REVERSE_DELTA_SUM  = 40u;
static const uint PT_VALIDATION_STAT_PDF_SUPPORT_PROBE_SUM       = 41u;
static const uint PT_VALIDATION_STAT_PDF_FORWARD_SUM             = 42u;
static const uint PT_VALIDATION_STAT_PDF_REVERSE_CONT_SUM        = 43u;
static const uint PT_VALIDATION_STAT_PDF_REVERSE_DELTA_SUM       = 44u;
static const uint PT_VALIDATION_STAT_SAMPLE_MAX_TOTAL            = 45u;
static const uint PT_VALIDATION_STAT_EVALUATE_MAX_TOTAL          = 46u;
static const uint PT_VALIDATION_STAT_PDF_MAX_TOTAL               = 47u;
static const uint PT_VALIDATION_STAT_COUNTER_OVERFLOW            = 48u;
static const uint PT_VALIDATION_STAT_COUNT                       = 49u;

static const uint PT_VALIDATION_QUERY_CONTINUATION    = 0u;
static const uint PT_VALIDATION_QUERY_FINITE_NEE      = 1u;
static const uint PT_VALIDATION_QUERY_ENVIRONMENT_NEE = 2u;

static const uint PT_VALIDATION_WALKER_SAMPLE   = 0u;
static const uint PT_VALIDATION_WALKER_EVALUATE = 1u;
static const uint PT_VALIDATION_WALKER_PDF      = 2u;

static const uint PT_VALIDATION_RETRY_NOT_APPLICABLE = 0u;
static const uint PT_VALIDATION_RETRY_ANY_POSITIVE   = 1u;
static const uint PT_VALIDATION_RETRY_ALL_ZERO       = 2u;

static const float PT_VALIDATION_NEAR_ZERO_PDF = 1.0 / 1048576.0;

void RecordValidationEventSum(RWStructuredBuffer< uint > Stats, uint index, uint value)
{
    uint oldValue;
    InterlockedAdd(Stats[index], value, oldValue);
    if (value > 0u && oldValue > 0xffffffffu - value)
        InterlockedMax(Stats[PT_VALIDATION_STAT_COUNTER_OVERFLOW], 1u);
}

void RecordValidationWalkerAudit(uint walkerKind, uint layerCount, BxDF::LayerWalkerAudit audit)
{
    if (layerCount <= 1u)
        return;

    RWStructuredBuffer< uint > Stats = GetResource(g_PathValidationStats.index);
    uint totalEvents = audit.supportProbeEvents + audit.forwardEvents +
        audit.reverseContinuousEvents + audit.reverseDeltaEvents;

    uint histogramBase = walkerKind == PT_VALIDATION_WALKER_SAMPLE
        ? PT_VALIDATION_STAT_SAMPLE_HISTORY_BASE
        : (walkerKind == PT_VALIDATION_WALKER_EVALUATE
            ? PT_VALIDATION_STAT_EVALUATE_HISTORY_BASE
            : PT_VALIDATION_STAT_PDF_HISTORY_BASE);
    uint maxIndex = walkerKind == PT_VALIDATION_WALKER_SAMPLE
        ? PT_VALIDATION_STAT_SAMPLE_MAX_TOTAL
        : (walkerKind == PT_VALIDATION_WALKER_EVALUATE
            ? PT_VALIDATION_STAT_EVALUATE_MAX_TOTAL
            : PT_VALIDATION_STAT_PDF_MAX_TOTAL);

    uint historyBin = totalEvents <= 3u ? 0u :
        (totalEvents <= 7u ? 1u :
        (totalEvents <= 15u ? 2u :
        (totalEvents <= 31u ? 3u :
        (totalEvents <= 63u ? 4u :
        (totalEvents <= 127u ? 5u : 6u)))));
    InterlockedAdd(Stats[histogramBase + historyBin], 1u);
    InterlockedMax(Stats[maxIndex], totalEvents);

    if (walkerKind == PT_VALIDATION_WALKER_SAMPLE)
    {
        RecordValidationEventSum(Stats, PT_VALIDATION_STAT_SAMPLE_FORWARD_SUM, audit.forwardEvents);
    }
    else if (walkerKind == PT_VALIDATION_WALKER_EVALUATE)
    {
        RecordValidationEventSum(Stats, PT_VALIDATION_STAT_EVALUATE_FORWARD_SUM, audit.forwardEvents);
        RecordValidationEventSum(Stats, PT_VALIDATION_STAT_EVALUATE_REVERSE_CONT_SUM, audit.reverseContinuousEvents);
        RecordValidationEventSum(Stats, PT_VALIDATION_STAT_EVALUATE_REVERSE_DELTA_SUM, audit.reverseDeltaEvents);
    }
    else
    {
        RecordValidationEventSum(Stats, PT_VALIDATION_STAT_PDF_SUPPORT_PROBE_SUM, audit.supportProbeEvents);
        RecordValidationEventSum(Stats, PT_VALIDATION_STAT_PDF_FORWARD_SUM, audit.forwardEvents);
        RecordValidationEventSum(Stats, PT_VALIDATION_STAT_PDF_REVERSE_CONT_SUM, audit.reverseContinuousEvents);
        RecordValidationEventSum(Stats, PT_VALIDATION_STAT_PDF_REVERSE_DELTA_SUM, audit.reverseDeltaEvents);
    }
}

uint DiagnoseValidationMarginalPDFZero(
    SurfaceMaterial rootMaterial,
    float2 uv,
    float2 ddxUV,
    float2 ddyUV,
    float3 wo,
    float3 wi,
    BoundaryMediumPair boundaryPair,
    uint querySeed,
    BxDF::MarginalPDFAudit audit)
{
    if (audit.state != BxDF::PT_MARGINAL_STATE_NLAYER_ZERO_CANDIDATE)
        return PT_VALIDATION_RETRY_NOT_APPLICABLE;

    bool anyPositive = false;
    [loop]
    for (uint retry = 0u; retry < 3u; ++retry)
    {
        uint retrySalt = retry == 0u ? 0xA511E9B3u : (retry == 1u ? 0x63D83595u : 0xB5297A4Du);
        BxDF::MarginalPDFAudit retryAudit;
        float retryPDF = BxDF::DirectionalComposite::MarginalPDF(
            rootMaterial,
            uv,
            ddxUV,
            ddyUV,
            wo,
            wi,
            boundaryPair,
            PCGHash(querySeed ^ retrySalt),
            retryAudit);
        anyPositive = anyPositive || retryPDF > 0.0;
    }
    return anyPositive ? PT_VALIDATION_RETRY_ANY_POSITIVE : PT_VALIDATION_RETRY_ALL_ZERO;
}

void RecordValidationMarginalPDF(
    uint queryKind,
    uint layerCount,
    float pdf,
    BxDF::MarginalPDFAudit audit,
    uint retryClassification)
{
    RWStructuredBuffer< uint > Stats = GetResource(g_PathValidationStats.index);
    uint queryBase = queryKind == PT_VALIDATION_QUERY_CONTINUATION
        ? PT_VALIDATION_STAT_CONTINUATION_TOTAL
        : (queryKind == PT_VALIDATION_QUERY_FINITE_NEE
            ? PT_VALIDATION_STAT_FINITE_NEE_TOTAL
            : PT_VALIDATION_STAT_ENVIRONMENT_NEE_TOTAL);

    InterlockedAdd(Stats[queryBase], 1u);
    if (pdf <= 0.0)
        InterlockedAdd(Stats[queryBase + 1u], 1u);
    else if (pdf <= PT_VALIDATION_NEAR_ZERO_PDF)
        InterlockedAdd(Stats[queryBase + 2u], 1u);

    if (audit.state == BxDF::PT_MARGINAL_STATE_NO_CONTINUOUS_PROPOSAL)
        InterlockedAdd(Stats[PT_VALIDATION_STAT_NO_CONTINUOUS_PROPOSAL], 1u);
    else if (audit.state == BxDF::PT_MARGINAL_STATE_SINGLE_LAYER && pdf <= 0.0)
        InterlockedAdd(Stats[PT_VALIDATION_STAT_SINGLE_LAYER_ZERO], 1u);
    else if (audit.state == BxDF::PT_MARGINAL_STATE_NLAYER_ZERO_CANDIDATE)
        InterlockedAdd(Stats[PT_VALIDATION_STAT_NLAYER_ZERO_CANDIDATE], 1u);
    else if (audit.state == BxDF::PT_MARGINAL_STATE_NLAYER_POSITIVE)
        InterlockedAdd(Stats[PT_VALIDATION_STAT_NLAYER_POSITIVE], 1u);
    else if (audit.state == BxDF::PT_MARGINAL_STATE_NONFINITE)
        InterlockedAdd(Stats[PT_VALIDATION_STAT_NONFINITE], 1u);

    if (retryClassification == PT_VALIDATION_RETRY_ANY_POSITIVE)
        InterlockedAdd(Stats[PT_VALIDATION_STAT_NLAYER_RETRY_ANY_POSITIVE], 1u);
    else if (retryClassification == PT_VALIDATION_RETRY_ALL_ZERO)
        InterlockedAdd(Stats[PT_VALIDATION_STAT_NLAYER_RETRY_ALL_ZERO], 1u);

    RecordValidationWalkerAudit(PT_VALIDATION_WALKER_PDF, layerCount, audit.walker);
}


struct PathValidationSums
{
    float3 albedo;
    float3 normal;
    float3 depth;
    float3 geometricNormal;
    float3 materialParams;
    float3 materialExtra;
    float3 materialSpecularColor;
    float3 emission;
    float3 diffuseRadiance;
    float3 specularRadiance;
    float3 transmissionRadiance;
    float3 surfaceLobeMask;
    float3 surfaceLobeWeight;
    float3 sampledLobeFrequency;
    float3 primaryId;
};

PathValidationSums ZeroPathValidationSums()
{
    PathValidationSums sums;
    sums.albedo                 = float3(0.0, 0.0, 0.0);
    sums.normal                 = float3(0.0, 0.0, 0.0);
    sums.depth                  = float3(0.0, 0.0, 0.0);
    sums.geometricNormal        = float3(0.0, 0.0, 0.0);
    sums.materialParams         = float3(0.0, 0.0, 0.0);
    sums.materialExtra          = float3(0.0, 0.0, 0.0);
    sums.materialSpecularColor  = float3(0.0, 0.0, 0.0);
    sums.emission               = float3(0.0, 0.0, 0.0);
    sums.diffuseRadiance        = float3(0.0, 0.0, 0.0);
    sums.specularRadiance       = float3(0.0, 0.0, 0.0);
    sums.transmissionRadiance   = float3(0.0, 0.0, 0.0);
    sums.surfaceLobeMask        = float3(0.0, 0.0, 0.0);
    sums.surfaceLobeWeight      = float3(0.0, 0.0, 0.0);
    sums.sampledLobeFrequency   = float3(0.0, 0.0, 0.0);
    sums.primaryId              = float3(0.0, 0.0, 0.0);
    return sums;
}

void AccumulatePathContribution(inout PathContribution contribution, uint flags, float3 value)
{
    if ((flags & PT_BSDF_FLAG_DIFFUSE) != 0u)
        contribution.diffuse += value;
    else if ((flags & PT_BSDF_FLAG_TRANSMISSION) != 0u)
        contribution.transmission += value;
    else if ((flags & PT_BSDF_FLAG_GLOSSY) != 0u)
        contribution.specular += value;
}

void AccumulateValidationContribution(inout PathValidationSums sums, PathContribution contribution)
{
    sums.diffuseRadiance      += contribution.diffuse;
    sums.specularRadiance     += contribution.specular;
    sums.transmissionRadiance += contribution.transmission;
}

void AccumulatePrimaryMissValidation(inout PathValidationSums sums)
{
    sums.normal          += float3(0.5, 0.5, 0.5);
    sums.geometricNormal += float3(0.5, 0.5, 0.5);
    sums.depth           += float3(g_Camera.zFar, g_Camera.zFar, g_Camera.zFar);
    sums.primaryId       += float3(0.0, 0.0, 0.0);
}

void PathValidationBuildONB(float3 n, out float3 t, out float3 b)
{
    const float sign = (n.z >= 0.0) ? 1.0 : -1.0;
    const float a = -1.0 / (sign + n.z);
    const float h = n.x * n.y * a;

    t = float3(1.0 + sign * n.x * n.x * a, sign * h, -sign * n.x);
    b = float3(h, sign + n.y * n.y * a, -n.y);
}
void AccumulatePrimaryHitValidation(inout PathValidationSums sums, SurfaceData primaryHit, RayDesc primaryRay, PathBSDFSample primaryBSDFSample)
{
    StructuredBuffer< InstanceData > Instances = GetResource(g_Instances.index);
    uint primaryMaterialID = Instances[primaryHit.instanceID].materialID;
    SurfaceMaterial primaryMaterial = LoadSurfaceMaterial(primaryMaterialID, primaryHit.uv, primaryHit.ddxUV, primaryHit.ddyUV, primaryHit.tangent.w);
    BxDF::Frame primaryFrame = MakeSurfaceFrame(primaryHit);
    float3 primaryWo = BxDF::ToLocal(primaryFrame, -primaryRay.Direction);

    sums.surfaceLobeMask        += BxDF::LayerComposite::SurfaceLobeMask(primaryMaterial, 1.0, primaryMaterial.ior);
    sums.surfaceLobeWeight      += BxDF::LayerComposite::SurfaceLobeWeight(primaryMaterial, primaryWo, 1.0, primaryMaterial.ior);
    sums.sampledLobeFrequency   += BxDF::LayerComposite::SampledLobeVector(primaryBSDFSample);
    sums.albedo                 += primaryMaterial.albedo;
    sums.normal                 += primaryHit.normal * 0.5 + 0.5;
    sums.geometricNormal        += primaryHit.geometricNormal * 0.5 + 0.5;
    sums.depth                  += float3(primaryHit.dist, primaryHit.dist, primaryHit.dist);
    sums.materialParams         += float3(primaryMaterial.roughness, primaryMaterial.metallic, primaryMaterial.transmission);
    sums.materialExtra          += float3(primaryMaterial.ior, float(ClassifyValidationBSDF(primaryMaterial)), primaryMaterial.anisotropy);
    sums.materialSpecularColor  += primaryMaterial.specularColor;
    sums.emission               += primaryMaterial.emission;
    sums.primaryId              += float3(
        primaryMaterialID == INVALID_INDEX ? 0.0 : float(primaryMaterialID + 1u),
        float(primaryHit.instanceID + 1u),
        float(primaryHit.primitiveID + 1u));
}

float3 AccumulatedValidationAverage(bool bReset, float3 previousAverage, float previousSamples, float3 sampleSum)
{
    return (bReset ? float3(0.0, 0.0, 0.0) : previousAverage * previousSamples) + sampleSum;
}

#endif // PT_VALIDATION

#endif // _HLSL_PATHVALIDATION_HEADER

