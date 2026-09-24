#ifndef _HLSL_PATHCOMPOSITE_HEADER
#define _HLSL_PATHCOMPOSITE_HEADER

#include "PathLayered.hlsli"
#include "Sampling.hlsli"

namespace BxDF
{



#if PT_VALIDATION
static const uint PT_MARGINAL_STATE_INVALID                = 0u;
static const uint PT_MARGINAL_STATE_NO_CONTINUOUS_PROPOSAL = 1u;
static const uint PT_MARGINAL_STATE_SINGLE_LAYER           = 2u;
static const uint PT_MARGINAL_STATE_NLAYER_POSITIVE        = 3u;
static const uint PT_MARGINAL_STATE_NLAYER_ZERO_CANDIDATE  = 4u;
static const uint PT_MARGINAL_STATE_NONFINITE              = 5u;

struct LayerWalkerAudit
{
    uint supportProbeEvents;
    uint forwardEvents;
    uint reverseContinuousEvents;
    uint reverseDeltaEvents;
};

struct MarginalPDFAudit
{
    LayerWalkerAudit walker;
    uint state;
};
#endif

namespace LayerComposite
{

static const uint MODEL_SLOT_DIFFUSE    = 0u;
static const uint MODEL_SLOT_SHEEN      = 1u;
static const uint MODEL_SLOT_CLEARCOAT  = 2u;
static const uint MODEL_SLOT_CONDUCTOR  = 3u;
static const uint MODEL_SLOT_DIELECTRIC = 4u;
static const uint MODEL_SLOT_COUNT      = 5u;

static const float MAX_RAY_CONE_FULL_ANGLE = 0.5 * PI;


bool IsSmoothConductor(SurfaceMaterial material)
{
    return !IsPrincipledMaterial(material) &&
           !HasTransmissionLobe(material) &&
           !HasClearcoatLobe(material) &&
           !HasSheenLobe(material) &&
           saturate(material.metallic) > 1.0 - PT_LOBE_EPS &&
           material.isSmooth != 0u;
}
float GetIncidentFrameSign(float3 wo, uint isIncidentRayEntering)
{
    bool isShadingFrameEntering = wo.z > 0.0;
    return isShadingFrameEntering == (isIncidentRayEntering != 0u) ? 1.0 : -1.0;
}

bool RefractConeBoundary(float2 incidentDir, float2 normal, float etaIOverT, out float2 transmittedDir)
{
    float NoI = dot(normal, incidentDir);
    float k = 1.0 - sq(etaIOverT) * (1.0 - sq(NoI));

    if (k < -EPSILON_MIN)
    {
        transmittedDir = incidentDir - normal * NoI;
        float tangentLenSq = dot(transmittedDir, transmittedDir);
        if (!IsPathFinite(tangentLenSq) || tangentLenSq <= EPSILON_MIN)
            return false;

        transmittedDir *= rsqrt(tangentLenSq);
        return IsPathFinite(transmittedDir.x) && IsPathFinite(transmittedDir.y);
    }
    transmittedDir = etaIOverT * incidentDir - normal * (etaIOverT * NoI + safeSqrt(max(k, 0.0)));

    float transmittedLenSq = dot(transmittedDir, transmittedDir);
    if (!IsPathFinite(transmittedLenSq) || transmittedLenSq <= EPSILON_MIN)
        return false;

    transmittedDir *= rsqrt(transmittedLenSq);
    return IsPathFinite(transmittedDir.x) && IsPathFinite(transmittedDir.y);
}

// Reference: https://raw.githubusercontent.com/NVIDIAGameWorks/Falcor/master/Source/Falcor/Rendering/Materials/TexLODHelpers.slang
bool UpdateTransmissionRayCone(float3 wo, Layered::LayerEvent event, inout RayCone cone)
{
    if (!IsPathFinite3(wo) || !IsPathFinite3(event.wi) ||
        !IsPathFinite(event.etaTOverI) || event.etaTOverI <= EPSILON_MIN ||
        !IsPathFinite(cone.radius) || !IsPathFinite(cone.tanHalfAngle))
    {
        return false;
    }

    if (abs(event.etaTOverI - 1.0) <= EPSILON_MIN)
        return true;

    float woLenSq = dot(wo, wo);
    float wiLenSq = dot(event.wi, event.wi);
    if (woLenSq <= EPSILON_MIN || wiLenSq <= EPSILON_MIN)
        return false;

    float3 incidentDir    = -wo * rsqrt(woLenSq);
    float3 transmittedDir = event.wi * rsqrt(wiLenSq);
    float3 opticalNormal;
    if (event.isDelta != 0u)
    {
        opticalNormal = wo.z >= 0.0 ? float3(0.0, 0.0, 1.0) : float3(0.0, 0.0, -1.0);
    }
    else
    {
        bool flipped = wo.z < 0.0;
        float3 woIncident = flipped ? -wo : wo;
        float3 wiIncident = flipped ? -event.wi : event.wi;

        float  resolvedEtaTOverI;
        float3 whIncident = BxDF::Lobe::Transmission::HalfVector(woIncident, wiIncident, event.etaTOverI, resolvedEtaTOverI);
        if (!IsPathFinite3(whIncident) || dot(whIncident, whIncident) <= EPSILON_MIN)
        {
            return false;
        }

        opticalNormal = flipped ? -whIncident : whIncident;
    }

    float3 tangent      = incidentDir - opticalNormal * dot(opticalNormal, incidentDir);
    float  tangentLenSq = dot(tangent, tangent);

    float3 xAxis;
    if (tangentLenSq > EPSILON_MIN)
    {
        xAxis = tangent * rsqrt(tangentLenSq);
    }
    else
    {
        float3 _;
        BuildSurfaceONB( opticalNormal, xAxis, _);
    }

    float2 incidentDir2D    = float2(dot(incidentDir, xAxis), dot(incidentDir, opticalNormal));
    float2 transmittedDir2D = float2(dot(transmittedDir, xAxis), dot(transmittedDir, opticalNormal));

    float incidentLenSq    = dot(incidentDir2D, incidentDir2D);
    float transmittedLenSq = dot(transmittedDir2D, transmittedDir2D);
    if (incidentLenSq <= EPSILON_MIN || transmittedLenSq <= EPSILON_MIN)
        return false;

    incidentDir2D    *= rsqrt(incidentLenSq);
    transmittedDir2D *= rsqrt(transmittedLenSq);

    float spreadAngle   = 2.0 * atan(cone.tanHalfAngle);
    float widthSign     = cone.radius > 0.0 ? 1.0 : -1.0;
    float boundaryAngle = 0.5 * spreadAngle * widthSign;

    float boundarySin;
    float boundaryCos;
    sincos(boundaryAngle, boundarySin, boundaryCos);

    float2 incidentUpper = float2(
        boundaryCos * incidentDir2D.x - boundarySin * incidentDir2D.y,
        boundarySin * incidentDir2D.x + boundaryCos * incidentDir2D.y);
    float2 incidentLower = float2(
        boundaryCos * incidentDir2D.x + boundarySin * incidentDir2D.y,
       -boundarySin * incidentDir2D.x + boundaryCos * incidentDir2D.y);

    float2 incidentOrtho = float2(-incidentDir2D.y, incidentDir2D.x);
    float2 upperOrigin =  incidentOrtho * cone.radius;
    float2 lowerOrigin = -upperOrigin;

    if (abs(incidentUpper.y) <= EPSILON_MIN || abs(incidentLower.y) <= EPSILON_MIN)
        return false;

    float upperHitX = upperOrigin.x + incidentUpper.x * (-upperOrigin.y / incidentUpper.y);
    float lowerHitX = lowerOrigin.x + incidentLower.x * (-lowerOrigin.y / incidentLower.y);
    if (!IsPathFinite(upperHitX) || !IsPathFinite(lowerHitX))
        return false;

    float normalSign = upperHitX > lowerHitX ? 1.0 : -1.0;
    float etaIOverT = rcp(event.etaTOverI); // LayerEvent stores iorT / iorI.
    float2 normal2D = float2(0.0, 1.0);
    float2 refractedUpper;
    float2 refractedLower;
    if (!RefractConeBoundary(incidentUpper, normal2D, etaIOverT, refractedUpper) ||
        !RefractConeBoundary(incidentLower, normal2D, etaIOverT, refractedLower))
    {
        return false;
    }

    float boundaryCross        = refractedUpper.x * refractedLower.y - refractedUpper.y * refractedLower.x;
    float spreadSign           = boundaryCross * normalSign < 0.0 ? 1.0 : -1.0;
    float refractedSpreadAngle = atan2(abs(boundaryCross), clamp(dot(refractedUpper, refractedLower), -1.0, 1.0)) * spreadSign;

    float2 transmittedOrtho    = float2(-transmittedDir2D.y, transmittedDir2D.x);
    float2 refractedUpperOrtho = float2(-refractedUpper.y, refractedUpper.x);
    float2 refractedLowerOrtho = float2(-refractedLower.y, refractedLower.x);

    float upperDenominator = dot(transmittedOrtho, refractedUpperOrtho);
    float lowerDenominator = dot(transmittedOrtho, refractedLowerOrtho);
    if (abs(upperDenominator) <= EPSILON_MIN || abs(lowerDenominator) <= EPSILON_MIN)
        return false;

    float refractedWidth =
            (-upperHitX * refractedUpper.y) / upperDenominator + (lowerHitX * refractedLower.y) / lowerDenominator;
    refractedSpreadAngle = clamp( refractedSpreadAngle, -MAX_RAY_CONE_FULL_ANGLE, MAX_RAY_CONE_FULL_ANGLE);

    RayCone candidate;
    candidate.radius       = 0.5 * refractedWidth;
    candidate.tanHalfAngle = tan(0.5 * refractedSpreadAngle);
    if (!IsPathFinite(candidate.radius) || !IsPathFinite(candidate.tanHalfAngle))
        return false;

    cone = candidate;
    return true;
}

// Reference: https://www.jcgt.org/published/0010/01/01/paper-lowres.pdf
void UpdateRayCone(SurfaceMaterial sm, float3 wo, Layered::LayerEvent event, float roughnessSpreadScale, inout RayCone cone)
{
    if (event.isTransmission != 0u)
    {
        RayCone candidate = cone;

        if (!UpdateTransmissionRayCone(wo, event, candidate))
            return;

        cone = candidate;

        if (event.isDelta != 0u)
            return;
    }
    else if (event.isDelta != 0u)
        return;

    float roughnessSpread;
    if (event.lobe == BxDF::LOBE_DIFFUSE || event.lobe == BxDF::LOBE_SHEEN)
    {
        roughnessSpread = MAX_RAY_CONE_FULL_ANGLE;
    }
    else
    {
        float alpha = event.lobe == BxDF::LOBE_CLEARCOAT ? GetClearcoatAlpha(sm) : max2(GetAlpha2(sm));
        if (alpha >= 1.0)
        {
            roughnessSpread = MAX_RAY_CONE_FULL_ANGLE;
        }
        else
        {
            float alphaSq = alpha * alpha;
            roughnessSpread = safeSqrt(
                0.5 * alphaSq /
                max(1.0 - alphaSq, EPSILON_MIN));
        }
    }

    float fullAngle = 2.0 * atan(cone.tanHalfAngle);
    fullAngle = clamp(fullAngle + roughnessSpreadScale * roughnessSpread, -MAX_RAY_CONE_FULL_ANGLE, MAX_RAY_CONE_FULL_ANGLE);

    cone.tanHalfAngle = tan(0.5 * fullAngle);
}

struct ModelMixture
{
    float diffusePMF;
    float sheenPMF;
    float clearcoatPMF;
    float conductorPMF;
    float dielectricPMF;
};

bool IsSupportedSmoothThinDielectric(SurfaceMaterial sm)
{
    return IsThinWalled(sm) &&
           sm.layerCount == 1u &&
           sm.isSmooth != 0u &&
           IsPathFinite(sm.ior) && sm.ior > 0.0 &&
           IsPathFinite(sm.metallic) && saturate(sm.metallic) <= PT_LOBE_EPS &&
           IsPathFinite(sm.transmission) && saturate(sm.transmission) >= 1.0 - PT_LOBE_EPS &&
           IsPathFinite(sm.clearcoat) && !HasClearcoatLobe(sm) &&
           IsPathFinite3(sm.sheenColor) && !HasSheenLobe(sm);
}

bool IsSupportedThinCompositeLayout(SurfaceMaterial rootMaterial)
{
    uint count = max(rootMaterial.layerCount, 1u);
    if (count == 1u)
        return !IsThinWalled(rootMaterial) || IsSupportedSmoothThinDielectric(rootMaterial);

    if (IsThinWalled(rootMaterial) || rootMaterial.layerOffset == INVALID_INDEX)
        return false;

    StructuredBuffer< MaterialSlabData > Slabs = GetResource(g_MaterialSlabs.index);
    StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);

    [loop]
    for (uint boundary = 1u; boundary < count; ++boundary)
    {
        MaterialSlabData slab = Slabs[rootMaterial.layerOffset + boundary];
        if (slab.materialID == INVALID_INDEX || (Materials[slab.materialID].materialFlags & MATERIAL_FLAG_THIN_WALLED) != 0u)
            return false;
    }

    return true;
}

Layered::DielectricFrame ResolveDielectricFrame(SurfaceMaterial sm, float3 wo, float ior1, float ior2)
{
    if (IsThinWalled(sm))
        return Layered::MakeThinDielectricFrame(wo, ior1, sm.ior);
    if (IsRelativeIORInterface(sm))
        return Layered::MakeDielectricFrame(wo, 1.0, sm.ior);
    return Layered::MakeDielectricFrame(wo, ior1, ior2);
}


float DielectricF0(float etaTOverI)
{
    etaTOverI = max(etaTOverI, 1.0e-4);
    float f0 = (etaTOverI - 1.0) / (etaTOverI + 1.0);
    return f0 * f0;
}

float3 ResolveConductorF0(SurfaceMaterial sm)
{
    return IsPrincipledMaterial(sm) ? saturate(sm.albedo) : saturate(sm.specularColor);
}

float ResolveConductorScale(SurfaceMaterial sm)
{
    float scale = saturate(sm.metallic);
    return scale > PT_LOBE_EPS ? scale : 0.0;
}

float3 ResolveDielectricReflectionScale(SurfaceMaterial sm, float etaTOverI)
{
    float dielectric = 1.0 - saturate(sm.metallic);
    float strength   = saturate(sm.specularStrength);
    if (dielectric <= PT_LOBE_EPS || strength <= PT_LOBE_EPS ||
        (!IsPrincipledMaterial(sm) && !HasDielectricSpecularLobe(sm, etaTOverI)))
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 scale =
            IsPrincipledMaterial(sm) ? float3(dielectric, dielectric, dielectric) : dielectric * strength * saturate(sm.specularColor);
    return max3(scale) > PT_LOBE_EPS ? scale : float3(0.0, 0.0, 0.0);
}

float3 ResolveDielectricF0(SurfaceMaterial sm, float etaTOverI)
{
    float baseF0 = DielectricF0(etaTOverI);
    if (!IsPrincipledMaterial(sm))
        return float3(baseF0, baseF0, baseF0);

    float strength = saturate(sm.specularStrength);
    return min(saturate(sm.specularColor) * strength, float3(strength, strength, strength));
}

float3 ResolveDielectricF90(SurfaceMaterial sm)
{
    float strength = IsPrincipledMaterial(sm) ? saturate(sm.specularStrength) : 1.0;
    return float3(strength, strength, strength);
}

float3 ResolveDielectricTransmissionScale(SurfaceMaterial sm)
{
    float scale = (1.0 - saturate(sm.metallic)) * saturate(sm.transmission);
    scale = scale > PT_LOBE_EPS ? scale : 0.0;
    return float3(scale, scale, scale);
}

float ResolveDiffuseScale(SurfaceMaterial sm)
{
    float scale = (1.0 - saturate(sm.metallic)) * (1.0 - saturate(sm.transmission));
    return scale > PT_LOBE_EPS ? scale : 0.0;
}


ModelMixture ResolveModelMixture(SurfaceMaterial sm, float3 wo, float etaTOverI)
{
    ModelMixture mixture;
    mixture.diffusePMF    = 0.0;
    mixture.sheenPMF      = 0.0;
    mixture.clearcoatPMF  = 0.0;
    mixture.conductorPMF  = 0.0;
    mixture.dielectricPMF = 0.0;

    if (IsThinWalled(sm))
    {
        if (IsSupportedSmoothThinDielectric(sm))
            mixture.dielectricPMF = 1.0;
        return mixture;
    }

    float metallic     = saturate(sm.metallic);
    float dielectric   = 1.0 - metallic;
    float transmission = saturate(sm.transmission);

    float opaqueDielectric = dielectric * (1.0 - transmission);
    float wDiffuse         = opaqueDielectric > PT_LOBE_EPS ? opaqueDielectric : 0.0;
    float wSheen           = HasSheenLobe(sm) ? max(SheenSamplingWeight(sm), PT_LOBE_EPS) : 0.0;
    float wClearcoat       =
            HasClearcoatLobe(sm) ? (IsPrincipledMaterial(sm) ? saturate(sm.clearcoat) * 0.25 : saturate(sm.clearcoat)) : 0.0;

    float  conductorScale    = ResolveConductorScale(sm);
    float3 conductorF        = BxDF::Fresnel::Schlick(ResolveConductorF0(sm), BxDF::AbsCosTheta(wo));
    float  conductorResponse = max3(conductorF);
    if (sm.isSmooth == 0u && conductorScale > PT_LOBE_EPS)
        conductorResponse = max(conductorResponse, PT_LOBE_EPS);
    float wConductor = conductorScale > PT_LOBE_EPS ? conductorScale * conductorResponse : 0.0;

    float3 dielectricReflectionScale   = ResolveDielectricReflectionScale(sm, etaTOverI);
    float3 dielectricTransmissionScale = ResolveDielectricTransmissionScale(sm);
    float3 dielectricF = BxDF::ScatteringModel::Dielectric::EvaluateFresnel(
        BxDF::CosTheta(wo),
        ResolveDielectricF0(sm, etaTOverI),
        ResolveDielectricF90(sm),
        etaTOverI);

    float wDielectricReflection =
        max3(max(dielectricReflectionScale * dielectricF, float3(0.0, 0.0, 0.0)));
    float wDielectricTransmission =
        max3(max(dielectricTransmissionScale * (1.0 - dielectricF), float3(0.0, 0.0, 0.0)));

    bool isRoughSolidDielectric = sm.isSmooth == 0u && abs(etaTOverI - 1.0) > EPSILON_MIN;
    if (isRoughSolidDielectric)
    {
        if (max3(dielectricReflectionScale) > PT_LOBE_EPS)
            wDielectricReflection = max(wDielectricReflection, PT_LOBE_EPS);
        if (max3(dielectricTransmissionScale) > PT_LOBE_EPS)
            wDielectricTransmission = max(wDielectricTransmission, PT_LOBE_EPS);
    }
    float wDielectric = wDielectricReflection + wDielectricTransmission;

    float weightSum = wDiffuse + wSheen + wClearcoat + wConductor + wDielectric;
    if (weightSum <= EPSILON_MIN)
        return mixture;

    float invWeightSum = rcp(weightSum);
    mixture.diffusePMF    = wDiffuse * invWeightSum;
    mixture.sheenPMF      = wSheen * invWeightSum;
    mixture.clearcoatPMF  = wClearcoat * invWeightSum;
    mixture.conductorPMF  = wConductor * invWeightSum;
    mixture.dielectricPMF = wDielectric * invWeightSum;
    return mixture;
}



float3 EvaluateDiffuseBRDF(SurfaceMaterial sm, float3 wo, float3 wi)
{
    return BxDF::ScatteringModel::Diffuse::Evaluate(wo, wi, sm.albedo, sm.roughness, ResolveDiffuseScale(sm), IsPrincipledMaterial(sm) ? 1u : 0u);
}

float3 EvaluateSheenBRDF(SurfaceMaterial sm, float3 wo, float3 wi)
{
    if (!HasSheenLobe(sm))
        return float3(0.0, 0.0, 0.0);

    return BxDF::ScatteringModel::Sheen::Evaluate(wo, wi, sm.sheenColor, sm.sheenRoughness, IsPrincipledMaterial(sm) ? 1u : 0u);
}

float3 EvaluateSpecularBRDF(SurfaceMaterial sm, float3 wo, float3 wi, float etaTOverI)
{
    float2 alpha = GetAlpha2(sm);
    float3 conductor = BxDF::ScatteringModel::Conductor::Evaluate(
        wo,
        wi,
        ResolveConductorF0(sm),
        ResolveConductorScale(sm),
        alpha.x,
        alpha.y);

    float3 dielectric = BxDF::ScatteringModel::Dielectric::EvaluateReflection(
        wo,
        wi,
        ResolveDielectricReflectionScale(sm, etaTOverI),
        ResolveDielectricF0(sm, etaTOverI),
        ResolveDielectricF90(sm),
        alpha.x,
        alpha.y,
        etaTOverI);

    return conductor + dielectric;
}

float3 EvaluateClearcoatBRDF(SurfaceMaterial sm, float3 wo, float3 wi)
{
    if (!HasClearcoatLobe(sm))
        return float3(0.0, 0.0, 0.0);

    return BxDF::ScatteringModel::Clearcoat::Evaluate(wo, wi, GetClearcoatAlpha(sm), saturate(sm.clearcoat), IsPrincipledMaterial(sm) ? 1u : 0u);
}


// wk * fk : 'physical' weighted bsdf
PathContribution EvaluateBoundaryLobes(SurfaceMaterial sm, float3 wo, float3 wi, float ior1, float ior2, uint transportMode)
{
    if (IsThinWalled(sm))
        return ZeroPathContribution();

    Layered::DielectricFrame frame = ResolveDielectricFrame(sm, wo, ior1, ior2);

    float3 woLayer    = BxDF::RotateXY(frame.wo, -GetAnisotropyRotation(sm));
    float3 wiIncident = frame.bFlipped != 0u ? -wi : wi;
    float3 wiLayer    = BxDF::RotateXY(wiIncident, -GetAnisotropyRotation(sm));

    PathContribution lobes = ZeroPathContribution();
    lobes.diffuse  = EvaluateDiffuseBRDF(sm, woLayer, wiLayer) + EvaluateSheenBRDF(sm, woLayer, wiLayer);
    lobes.specular = EvaluateSpecularBRDF(sm, woLayer, wiLayer, frame.etaTOverI) + EvaluateClearcoatBRDF(sm, woLayer, wiLayer);

    float3 transmissionScale = ResolveDielectricTransmissionScale(sm);
    if (max3(transmissionScale) > PT_LOBE_EPS && !BxDF::SameHemisphere(woLayer, wiLayer))
    {
        float2 alpha = GetAlpha2(sm);
        lobes.transmission = BxDF::ScatteringModel::Dielectric::EvaluateTransmission(
            woLayer,
            wiLayer,
            transmissionScale,
            ResolveDielectricF0(sm, frame.etaTOverI),
            ResolveDielectricF90(sm),
            alpha.x,
            alpha.y,
            frame.etaTOverI,
            transportMode);
    }

    return lobes;
}

PathContribution EvaluateBoundaryLobes(SurfaceMaterial sm, float3 wo, float3 wi, float ior1, float ior2)
{
    return EvaluateBoundaryLobes(sm, wo, wi, ior1, ior2, PT_TRANSPORT_RADIANCE);
}

// 'proposal' bsdf's sampling pdf
float BoundaryMarginalPDF(SurfaceMaterial sm, float3 wo, float3 wi, float ior1, float ior2)
{
    if (IsThinWalled(sm))
        return 0.0;

    Layered::DielectricFrame frame = ResolveDielectricFrame(sm, wo, ior1, ior2);

    float3 woLayer    = BxDF::RotateXY(frame.wo, -GetAnisotropyRotation(sm));
    float3 wiIncident = frame.bFlipped != 0u ? -wi : wi;
    float3 wiLayer    = BxDF::RotateXY(wiIncident, -GetAnisotropyRotation(sm));

    float2 alpha = GetAlpha2(sm);

    ModelMixture mixture = ResolveModelMixture(sm, woLayer, frame.etaTOverI);
    float pdf = 0.0;

    pdf += mixture.diffusePMF * BxDF::ScatteringModel::Diffuse::EvaluatePDF(woLayer, wiLayer);
    pdf += mixture.sheenPMF * BxDF::ScatteringModel::Sheen::EvaluatePDF(woLayer, wiLayer);
    pdf += mixture.clearcoatPMF * BxDF::ScatteringModel::Clearcoat::EvaluatePDF(woLayer, wiLayer, GetClearcoatAlpha(sm));
    pdf += mixture.conductorPMF * BxDF::ScatteringModel::Conductor::EvaluatePDF(woLayer, wiLayer, alpha.x, alpha.y);
    pdf += mixture.dielectricPMF *
            BxDF::ScatteringModel::Dielectric::EvaluatePDF(
                woLayer,
                wiLayer,
                ResolveDielectricReflectionScale(sm, frame.etaTOverI),
                ResolveDielectricTransmissionScale(sm),
                ResolveDielectricF0(sm, frame.etaTOverI),
                ResolveDielectricF90(sm),
                alpha.x,
                alpha.y,
                frame.etaTOverI);

    return pdf;
}

float BoundaryMarginalDeltaPMF(SurfaceMaterial sm, float3 wo, float3 wi, float ior1, float ior2)
{
    Layered::DielectricFrame frame = ResolveDielectricFrame(sm, wo, ior1, ior2);

    float3 woLayer    = BxDF::RotateXY(frame.wo, -GetAnisotropyRotation(sm));
    float3 wiIncident = frame.bFlipped != 0u ? -wi : wi;
    float3 wiLayer    = BxDF::RotateXY(wiIncident, -GetAnisotropyRotation(sm));

    ModelMixture mixture = ResolveModelMixture(sm, woLayer, frame.etaTOverI);
    if (IsThinWalled(sm))
    {
        return mixture.dielectricPMF * BxDF::ScatteringModel::Dielectric::Thin::EvaluateDeltaPMF(
            woLayer,
            wiLayer,
            float3(1.0, 1.0, 1.0),
            float3(1.0, 1.0, 1.0),
            frame.etaTOverI);
    }

    float2 alpha = GetAlpha2(sm);
    float deltaPMF = mixture.conductorPMF * BxDF::ScatteringModel::Conductor::EvaluateDeltaPMF(
        woLayer,
        wiLayer,
        ResolveConductorScale(sm),
        alpha.x,
        alpha.y);
    deltaPMF += mixture.dielectricPMF * BxDF::ScatteringModel::Dielectric::EvaluateDeltaPMF(
        woLayer,
        wiLayer,
        ResolveDielectricReflectionScale(sm, frame.etaTOverI),
        ResolveDielectricTransmissionScale(sm),
        ResolveDielectricF0(sm, frame.etaTOverI),
        ResolveDielectricF90(sm),
        alpha.x,
        alpha.y,
        frame.etaTOverI);
    return deltaPMF;
}

uint ChooseModel(ModelMixture mixture, float uc, out float modelPMF)
{
    modelPMF = 0.0;
    float cumulative = mixture.diffusePMF;
    if (mixture.diffusePMF > 0.0 && uc < cumulative)
    {
        modelPMF = mixture.diffusePMF;
        return MODEL_SLOT_DIFFUSE;
    }

    cumulative += mixture.sheenPMF;
    if (mixture.sheenPMF > 0.0 && uc < cumulative)
    {
        modelPMF = mixture.sheenPMF;
        return MODEL_SLOT_SHEEN;
    }

    cumulative += mixture.clearcoatPMF;
    if (mixture.clearcoatPMF > 0.0 && uc < cumulative)
    {
        modelPMF = mixture.clearcoatPMF;
        return MODEL_SLOT_CLEARCOAT;
    }

    cumulative += mixture.conductorPMF;
    if (mixture.conductorPMF > 0.0 && uc < cumulative)
    {
        modelPMF = mixture.conductorPMF;
        return MODEL_SLOT_CONDUCTOR;
    }

    if (mixture.dielectricPMF > 0.0)
    {
        modelPMF = mixture.dielectricPMF;
        return MODEL_SLOT_DIELECTRIC;
    }

    return MODEL_SLOT_COUNT;
}

Layered::LayerEvent SampleLayerEvent(
    SurfaceMaterial sm,
    float3 wo,
    float ior1,
    float ior2,
    uint transportMode,
    inout RngState rng)
{
    Layered::LayerEvent event = Layered::InitializeLayerEvent();
    if (IsThinWalled(sm) && !IsSupportedSmoothThinDielectric(sm))
        return event;

    bool isThinWalled = IsThinWalled(sm);
    Layered::DielectricFrame frame = ResolveDielectricFrame(sm, wo, ior1, ior2);
    float3 woLayer = BxDF::RotateXY(frame.wo, -GetAnisotropyRotation(sm));

    ModelMixture mixture = ResolveModelMixture(sm, woLayer, frame.etaTOverI);
    float modelPMF;
    uint model = ChooseModel(mixture, NextFloat(rng), modelPMF);
    if (!IsPathFinite(modelPMF) || modelPMF <= 0.0)
        return event;

    float2 uDirection = NextFloat2(rng);
    BxDF::ScatteringModelSample bs = (BxDF::ScatteringModelSample)0;
    switch (model)
    {
        case MODEL_SLOT_DIFFUSE:
        {
            bs = BxDF::ScatteringModel::Diffuse::Sample(
                woLayer,
                sm.albedo,
                sm.roughness,
                ResolveDiffuseScale(sm),
                IsPrincipledMaterial(sm) ? 1u : 0u,
                uDirection);
        }
        break;

        case MODEL_SLOT_SHEEN:
        {
            bs = BxDF::ScatteringModel::Sheen::Sample(
                woLayer,
                sm.sheenColor,
                sm.sheenRoughness,
                IsPrincipledMaterial(sm) ? 1u : 0u,
                uDirection);
        }
        break;

        case MODEL_SLOT_CLEARCOAT:
        {
            bs = BxDF::ScatteringModel::Clearcoat::Sample(
                woLayer,
                GetClearcoatAlpha(sm),
                saturate(sm.clearcoat),
                IsPrincipledMaterial(sm) ? 1u : 0u,
                uDirection);
        }
        break;

        case MODEL_SLOT_CONDUCTOR:
        {
            float2 alpha = GetAlpha2(sm);
            bs = BxDF::ScatteringModel::Conductor::Sample(
                woLayer,
                ResolveConductorF0(sm),
                ResolveConductorScale(sm),
                alpha.x,
                alpha.y,
                uDirection);
        }
        break;

        case MODEL_SLOT_DIELECTRIC:
        {
            float uBranch = NextFloat(rng);
            if (isThinWalled)
            {
                bs = BxDF::ScatteringModel::Dielectric::Thin::Sample(
                    woLayer,
                    float3(1.0, 1.0, 1.0),
                    float3(1.0, 1.0, 1.0),
                    frame.etaTOverI,
                    uBranch);
            }
            else
            {
                float2 alpha = GetAlpha2(sm);
                bs = BxDF::ScatteringModel::Dielectric::Sample(
                    woLayer,
                    ResolveDielectricReflectionScale(sm, frame.etaTOverI),
                    ResolveDielectricTransmissionScale(sm),
                    ResolveDielectricF0(sm, frame.etaTOverI),
                    ResolveDielectricF90(sm),
                    alpha.x,
                    alpha.y,
                    frame.etaTOverI,
                    transportMode,
                    float3(uDirection, uBranch));
            }
        }
        break;

        default:
            return event;
    }

    // layer frame -> incident(or transmitted)-side frame
    float3 wiIncident = BxDF::RotateXY(bs.wi, GetAnisotropyRotation(sm));
    event.wi             = frame.bFlipped != 0u ? -wiIncident : wiIncident;
    event.isDelta        = bs.isDelta;
    event.isTransmission = BxDF::SameHemisphere(woLayer, bs.wi) ? 0u : 1u;
    event.etaTOverI      = event.isTransmission != 0u && !isThinWalled ? frame.etaTOverI : 1.0;

    if (bs.isDelta != 0u)
    {
        event.pdf    = modelPMF * bs.pdf;
        event.weight = bs.weight / modelPMF;
    }
    else
    {
        float mixturePDF = BoundaryMarginalPDF(sm, wo, event.wi, ior1, ior2);
        if (!IsPathFinite(mixturePDF) || mixturePDF <= 0.0)
            return Layered::InitializeLayerEvent();

        PathContribution lobes = EvaluateBoundaryLobes(
            sm,
            wo,
            event.wi,
            ior1,
            ior2,
            transportMode);
        float3 f = lobes.diffuse + lobes.specular + lobes.transmission;

        event.pdf    = mixturePDF;
        event.weight = f * BxDF::AbsCosTheta(bs.wi) / mixturePDF;
    }

    if (!IsPathFinite3(event.wi) || dot(event.wi, event.wi) <= EPSILON_MIN ||
        !IsPathFinite(event.pdf) || event.pdf <= 0.0 ||
        !IsPathFinite3(event.weight) || any(event.weight < 0.0) ||
        !IsPathFinite(event.etaTOverI) || event.etaTOverI <= 0.0)
        return Layered::InitializeLayerEvent();

    event.lobe  = bs.lobe;
    event.flags = bs.lobe == BxDF::LOBE_DIFFUSE || bs.lobe == BxDF::LOBE_SHEEN ?
            PT_BSDF_FLAG_DIFFUSE : event.isTransmission != 0u ? PT_BSDF_FLAG_TRANSMISSION : PT_BSDF_FLAG_GLOSSY;
    event.valid = 1u;
    return event;
}


bool TryResolveTerminalMedium(
    SurfaceMaterial rootMaterial,
    uint rootMaterialID,
    float2 uv,
    float2 ddxUV,
    float2 ddyUV,
    out Medium terminalMedium,
    out bool isTerminalMedium)
{
    isTerminalMedium = HasTransmissionLobe(rootMaterial);

    uint count = max(rootMaterial.layerCount, 1u);
    if (count == 1u)
    {
        terminalMedium.mediumID = rootMaterialID;
        terminalMedium.ior      = rootMaterial.ior;
    }
    else
    {
        if (rootMaterial.layerOffset == INVALID_INDEX)
            return false;

        StructuredBuffer< MaterialSlabData > Slabs = GetResource(g_MaterialSlabs.index);
        StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);

        [loop]
        for (uint boundary = 1u; boundary < count; ++boundary)
        {
            MaterialSlabData slab = Slabs[rootMaterial.layerOffset + boundary];
            if (slab.materialID == INVALID_INDEX)
                return false;

            SurfaceMaterial boundaryMaterial = LoadSurfaceMaterial(
                slab.materialID,
                uv,
                ddxUV,
                ddyUV,
                rootMaterial.tangentFrameSign);
            if (IsThinWalled(boundaryMaterial))
                return false;

            isTerminalMedium = isTerminalMedium && HasTransmissionLobe(boundaryMaterial);
            if (boundary == count - 1u)
            {
                terminalMedium.mediumID = slab.materialID;
                terminalMedium.ior      = max(Materials[slab.materialID].ior, 1.0e-4);
            }
        }
    }

    if (!IsPathFinite(terminalMedium.ior) || terminalMedium.ior <= 0.0)
        return false;

    return true;
}

PathBSDFSample SampleRay(
    SurfaceMaterial rootMaterial,
    float2 uv,
    float2 ddxUV,
    float2 ddyUV,
    float3 wo,
    BoundaryMediumPair boundaryPair,
    uint rrStartDepth,
    float roughnessSpreadScale,
    inout RayCone rayCone,
    inout RngState rng
#if PT_VALIDATION
    , out LayerWalkerAudit audit
#endif
)
{
#if PT_VALIDATION
    audit = (LayerWalkerAudit)0;
#endif

    PathBSDFSample s = (PathBSDFSample)0;
    s.attempted  = 1u;
    s.rrEtaScale = 1.0;

    float iorExterior = boundaryPair.isEntering != 0u ? boundaryPair.mediumI.ior : boundaryPair.mediumT.ior;
    if (!IsPathFinite3(wo) || abs(wo.z) <= EPSILON_MIN || !IsPathFinite(iorExterior) || iorExterior <= 0.0)
        return s;

    if (!IsSupportedThinCompositeLayout(rootMaterial))
        return s;

    StructuredBuffer< MaterialSlabData > Slabs = GetResource(g_MaterialSlabs.index);
    StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);

    float incidentFrameSign = GetIncidentFrameSign(wo, boundaryPair.isEntering);
    wo *= incidentFrameSign;

    int  count    = int(max(rootMaterial.layerCount, 1u));
    uint offset   = rootMaterial.layerOffset;
    int  boundary = wo.z > 0.0 ? 0 : count - 1;

    float3 w = -wo;

    float3 beta       = float3(1.0, 1.0, 1.0);
    float  rrEtaScale = 1.0;

    bool allDelta      = true;
    uint depth         = 0u;
    uint historyFlags  = 0u;

    for (;;)
    {
        SurfaceMaterial sm = rootMaterial;
        if (boundary > 0)
        {
            MaterialSlabData slab = Slabs[offset + boundary];
            sm = LoadSurfaceMaterial(slab.materialID, uv, ddxUV, ddyUV, rootMaterial.tangentFrameSign);
        }

        float ior1 = iorExterior;
        if (boundary > 0)
        {
            MaterialSlabData aboveSlab = Slabs[offset + boundary - 1];
            if (aboveSlab.materialID == INVALID_INDEX)
                return s;

            ior1 = max(Materials[aboveSlab.materialID].ior, 1.0e-4);
        }
        float ior2 = max(sm.ior, 1.0e-4);

#if PT_VALIDATION
        ++audit.forwardEvents;
#endif
        Layered::LayerEvent event = SampleLayerEvent(sm, -w, ior1, ior2, PT_TRANSPORT_RADIANCE, rng);
        if (event.valid == 0u)
            return s;

        UpdateRayCone(sm, -w, event, roughnessSpreadScale, rayCone);

        w     = event.wi;
        beta *= event.weight;

        allDelta     = allDelta && event.isDelta != 0u;
        historyFlags |= event.flags;

        if (event.isTransmission != 0u)
            rrEtaScale *= sq(event.etaTOverI);

        if (!IsPathFinite3(beta) || !any(beta > 0.0) ||
            !IsPathFinite(rrEtaScale) || abs(w.z) <= EPSILON_MIN)
            return s;

        int nextBoundary = boundary + (w.z < 0.0 ? 1 : -1);
        if (nextBoundary < 0 || nextBoundary >= count)
        {
            s.wi     = w * incidentFrameSign;
            s.weight = beta;

            s.flags      = historyFlags;
            s.lobe       = event.lobe;
            s.isDelta    = allDelta ? 1u : 0u;
            s.rrEtaScale = rrEtaScale;

            s.valid = 1u;
            return s;
        }

        int slabIndex = min(boundary, nextBoundary);
        MaterialSlabData medium = Slabs[offset + slabIndex];
        float distance = medium.thickness / max(abs(w.z), EPSILON_MIN);
        const float rrImportance = max3(beta * rrEtaScale);
        // volume extinction
        float3 sigmaA = float3(medium.sigmaA_r, medium.sigmaA_g, medium.sigmaA_b);
        beta *= exp(-sigmaA * distance);
        if (!IsPathFinite3(beta) || !any(beta > 0.0))
            return s;

        boundary = nextBoundary;
        ++depth;



        const float rrThreshold = 0.05;
        // This depth also includes the entrance transmission; PBRT's does not.
        if (depth > rrStartDepth + 1u && rrImportance < 0.25)
        {
            float qSurvive = max(rrImportance, rrThreshold);
            if (NextFloat(rng) >= qSurvive)
                return s;

            beta /= qSurvive;
        }
    }

    return s;
}

#if PT_VALIDATION
float3 SurfaceLobeMask(SurfaceMaterial material, float ior1, float ior2)
{
    float etaTOverI = IsRelativeIORInterface(material) ?
        max(material.ior, 1.0e-4) : max(ior2, 1.0e-4) / max(ior1, 1.0e-4);
    ModelMixture mixture = ResolveModelMixture(material, float3(0.0, 0.0, 1.0), etaTOverI);
    bool hasDielectricReflection = IsThinWalled(material) ||
        max3(ResolveDielectricReflectionScale(material, etaTOverI)) > PT_LOBE_EPS;
    bool hasDielectricTransmission = IsThinWalled(material) ||
        max3(ResolveDielectricTransmissionScale(material)) > PT_LOBE_EPS;
    return float3(
        mixture.diffusePMF + mixture.sheenPMF > PT_LOBE_EPS ? 1.0 : 0.0,
        mixture.clearcoatPMF + mixture.conductorPMF > PT_LOBE_EPS ||
            (mixture.dielectricPMF > PT_LOBE_EPS && hasDielectricReflection) ? 1.0 : 0.0,
        mixture.dielectricPMF > PT_LOBE_EPS && hasDielectricTransmission ? 1.0 : 0.0);
}

float3 SurfaceLobeWeight(SurfaceMaterial material, float3 wo, float ior1, float ior2)
{
    Layered::DielectricFrame frame = ResolveDielectricFrame(material, wo, ior1, ior2);
    ModelMixture mixture = ResolveModelMixture(material, frame.wo, frame.etaTOverI);

    float2 dielectricBranchPMF;
    if (IsThinWalled(material))
    {
        dielectricBranchPMF = BxDF::ScatteringModel::Dielectric::Thin::ResolveBranchPMF(
            frame.wo, float3(1.0, 1.0, 1.0), float3(1.0, 1.0, 1.0), frame.etaTOverI);
    }
    else
    {
        dielectricBranchPMF = BxDF::ScatteringModel::Dielectric::ResolveBranchPMF(
            frame.wo,
            float3(0.0, 0.0, 1.0),
            ResolveDielectricReflectionScale(material, frame.etaTOverI),
            ResolveDielectricTransmissionScale(material),
            ResolveDielectricF0(material, frame.etaTOverI),
            ResolveDielectricF90(material),
            frame.etaTOverI,
            1u,
            1u);
    }

    return float3(
        mixture.diffusePMF + mixture.sheenPMF,
        mixture.clearcoatPMF + mixture.conductorPMF + mixture.dielectricPMF * dielectricBranchPMF.x,
        mixture.dielectricPMF * dielectricBranchPMF.y);
}

float3 SampledLobeVector(PathBSDFSample sample)
{
    if (sample.attempted == 0u)
        return float3(0.0, 0.0, 0.0);
    if (sample.lobe == BxDF::LOBE_DIFFUSE || sample.lobe == BxDF::LOBE_SHEEN)
        return float3(1.0, 0.0, 0.0);
    if (sample.lobe == BxDF::LOBE_TRANSMISSION)
        return float3(0.0, 0.0, 1.0);
    return float3(0.0, 1.0, 0.0);
}

#endif // PT_VALIDATION

} // namespace LayerComposite


namespace DirectionalComposite
{

static const uint  EVALUATE_QUERY_SALT        = 0x243F6A88u;
static const uint  PDF_QUERY_SALT             = 0x85A308D3u;
static const uint  DIRECTIONAL_RR_START_DEPTH = 8u;
static const float DIRECTIONAL_RR_SURVIVAL    = 0.95;

bool TryResolveShadowStartMedium(
    float3 Ng,
    float3 woWS,
    float3 wiWS,
    float3 wo,
    float3 wi,
    BoundaryMediumPair boundaryPair,
    out Medium shadowStartMedium)
{
    float NgoWo = dot(Ng, woWS);
    float NgoWi = dot(Ng, wiWS);
    if (abs(NgoWi) <= EPSILON_MIN || abs(wi.z) <= EPSILON_MIN)
        return false;

    bool isGeometricTransmission  = (NgoWo > 0.0) != (NgoWi > 0.0);
    bool isScatteringTransmission = (wo.z > 0.0) != (wi.z > 0.0);
    if (isGeometricTransmission != isScatteringTransmission)
        return false;

    if (isGeometricTransmission)
        shadowStartMedium = boundaryPair.mediumT;
    else
        shadowStartMedium = boundaryPair.mediumI;
    return true;
}

float ExtendPowerStrategyRatioSum(float ratioSum, float numerator, float denominator)
{
    // (ratioSum + 1) * (numerator / denominator)^2, evaluated in log2 space
    // so a very narrow rough interface cannot overflow the MIS denominator.
    float log2Value = log2(max(ratioSum + 1.0, 1.0e-30)) +
                      2.0 * (log2(numerator) - log2(denominator));
    return exp2(clamp(log2Value, -100.0, 100.0));
}

RngState InitDirectionalQueryRng(
    uint querySeed,
    float3 wo,
    float3 wi,
    uint layerOffset,
    uint layerCount,
    uint salt,
    uint streamIndex)
{
    uint3 woBits = asuint(wo);
    uint3 wiBits = asuint(wi);

    uint seed = PCGHash(querySeed ^ salt);
    seed = PCGHash(seed ^ woBits.x);
    seed = PCGHash(seed ^ woBits.y);
    seed = PCGHash(seed ^ woBits.z);
    seed = PCGHash(seed ^ wiBits.x);
    seed = PCGHash(seed ^ wiBits.y);
    seed = PCGHash(seed ^ wiBits.z);
    seed = PCGHash(seed ^ layerOffset);
    seed = PCGHash(seed ^ layerCount);
    seed = PCGHash(seed ^ PCGHash(streamIndex + 0x9E3779B9u));

    RngState rng;
    rng.seed        = seed;
    rng.counter     = 0u;
    rng.sobolIndex  = 0u;
    rng.scrambleKey = seed;
    rng.sobolLimit  = 0u;
    rng.mode        = RNG_MODE_PCG; // fixed-endpoint queries stay on the hash stream
    return rng;
}

bool LoadBoundaryData(
    SurfaceMaterial rootMaterial,
    float2 uv,
    float2 ddxUV,
    float2 ddyUV,
    float iorExterior,
    int boundary,
    uint layerOffset,
    out SurfaceMaterial material,
    out float ior1,
    out float ior2)
{
    material = rootMaterial;
    ior1 = max(iorExterior, 1.0e-4);
    ior2 = max(rootMaterial.ior, 1.0e-4);

    if (boundary < 0)
        return false;
    if (boundary == 0)
        return true;
    if (layerOffset == INVALID_INDEX)
        return false;

    StructuredBuffer< MaterialSlabData > Slabs = GetResource(g_MaterialSlabs.index);
    StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);

    MaterialSlabData belowSlab = Slabs[layerOffset + boundary];
    MaterialSlabData aboveSlab = Slabs[layerOffset + boundary - 1];
    if (belowSlab.materialID == INVALID_INDEX || aboveSlab.materialID == INVALID_INDEX)
        return false;

    material = LoadSurfaceMaterial(belowSlab.materialID, uv, ddxUV, ddyUV, rootMaterial.tangentFrameSign);
    if (IsThinWalled(material))
        return false;

    ior1 = max(Materials[aboveSlab.materialID].ior, 1.0e-4);
    ior2 = max(material.ior, 1.0e-4);
    return true;
}

float3 Evaluate(
    SurfaceMaterial rootMaterial,
    float2 uv,
    float2 ddxUV,
    float2 ddyUV,
    float3 wo,
    float3 wi,
    BoundaryMediumPair boundaryPair,
    uint querySeed
#if PT_VALIDATION
    , out LayerWalkerAudit audit
#endif
)
{
#if PT_VALIDATION
    audit = (LayerWalkerAudit)0;
#endif

    const float3 zero = float3(0.0, 0.0, 0.0);

    float iorExterior = boundaryPair.isEntering != 0u ? boundaryPair.mediumI.ior : boundaryPair.mediumT.ior;
    if (!IsPathFinite3(wo) || !IsPathFinite3(wi) || abs(wo.z) <= EPSILON_MIN || abs(wi.z) <= EPSILON_MIN || !IsPathFinite(iorExterior) || iorExterior <= 0.0)
        return zero;

    if (IsThinWalled(rootMaterial))
        return zero;

    if (!LayerComposite::IsSupportedThinCompositeLayout(rootMaterial))
        return zero;

    StructuredBuffer< MaterialSlabData > Slabs = GetResource(g_MaterialSlabs.index);

    int  count  = int(max(rootMaterial.layerCount, 1u));
    uint offset = rootMaterial.layerOffset;
    float incidentFrameSign = LayerComposite::GetIncidentFrameSign(wo, boundaryPair.isEntering);
    wo *= incidentFrameSign;
    wi *= incidentFrameSign;

    int entryBoundary = wo.z > 0.0 ? 0 : count - 1;
    int exitBoundary  = wi.z > 0.0 ? 0 : count - 1;

    SurfaceMaterial exitMaterial;
    float exitEtaAbove;
    float exitEtaBelow;
    if (!LoadBoundaryData(
            rootMaterial,
            uv,
            ddxUV,
            ddyUV,
            iorExterior,
            exitBoundary,
            offset,
            exitMaterial,
            exitEtaAbove,
            exitEtaBelow))
    {
        return zero;
    }

    float3 result = zero;
    // The zero-internal-event boundary term is deterministic.
    if (entryBoundary == exitBoundary)
    {
        PathContribution directLobes = LayerComposite::EvaluateBoundaryLobes(
            exitMaterial,
            wo,
            wi,
            exitEtaAbove,
            exitEtaBelow);
        result += directLobes.diffuse + directLobes.specular + directLobes.transmission;
    }

    if (count == 1)
        return IsPathFinite3(result) && all(result >= 0.0) ? result : zero;

    RngState forwardRng = InitDirectionalQueryRng(
        querySeed,
        wo,
        wi,
        offset,
        uint(count),
        EVALUATE_QUERY_SALT,
        0u);

    int    forwardBoundary            = entryBoundary;
    float3 forwardW                   = -wo;
    float3 forwardBeta                = float3(1.0, 1.0, 1.0);
    bool   bForwardMISCompatible      = true;
    bool   bForwardHasContinuousEvent = false;
    float  forwardLeftRatioBase       = 0.0;
    float  forwardPreviousPDF         = 0.0;
    uint   forwardDepth               = 0u;

    [loop]
    for (;;)
    {
        // Continuous histories are sampled once from every connection split.
        // The power weights form a pointwise partition over those techniques.
        if (bForwardMISCompatible)
        {
            RngState reverseRng = InitDirectionalQueryRng(
                querySeed,
                wo,
                wi,
                offset,
                uint(count),
                EVALUATE_QUERY_SALT,
                0x10000000u + forwardDepth);

            int    reverseBoundary            = exitBoundary;
            float3 reverseW                   = -wi;
            float3 reverseBeta                = float3(1.0, 1.0, 1.0);
            bool   bReverseHasContinuousEvent = false;
            float  reverseRightRatioBase      = 0.0;
            float  reversePreviousPDF         = 0.0;
            uint   reverseDepth               = 0u;

            [loop]
            for (;;)
            {
                SurfaceMaterial reverseMaterial;
                float reverseEtaAbove;
                float reverseEtaBelow;
                if (!LoadBoundaryData(
                        rootMaterial,
                        uv,
                        ddxUV,
                        ddyUV,
                        iorExterior,
                        reverseBoundary,
                        offset,
                        reverseMaterial,
                        reverseEtaAbove,
                        reverseEtaBelow))
                {
                    return zero;
                }

                // The analytic boundary term owns only the literal zero-event path.
                if (reverseBoundary == forwardBoundary &&
                    (forwardDepth != 0u || reverseDepth != 0u))
                {
                    float3 connectionWo = -forwardW;
                    float3 connectionWi = -reverseW;

                    PathContribution connectionLobes = LayerComposite::EvaluateBoundaryLobes(
                        reverseMaterial,
                        connectionWo,
                        connectionWi,
                        reverseEtaAbove,
                        reverseEtaBelow);
                    float3 connection =
                        connectionLobes.diffuse +
                        connectionLobes.specular +
                        connectionLobes.transmission;

                    float connectionForwardPDF = LayerComposite::BoundaryMarginalPDF(
                        reverseMaterial,
                        connectionWo,
                        connectionWi,
                        reverseEtaAbove,
                        reverseEtaBelow);
                    float connectionReversePDF = LayerComposite::BoundaryMarginalPDF(
                        reverseMaterial,
                        connectionWi,
                        connectionWo,
                        reverseEtaAbove,
                        reverseEtaBelow);

                    bool bConnectionSupported =
                        IsPathFinite3(connection) &&
                        any(connection > 0.0) &&
                        IsPathFinite(connectionForwardPDF) &&
                        IsPathFinite(connectionReversePDF) &&
                        connectionForwardPDF > 0.0 &&
                        connectionReversePDF > 0.0;

                    if (bConnectionSupported)
                    {
                        float leftRatioSum = bForwardHasContinuousEvent
                            ? ExtendPowerStrategyRatioSum(
                                forwardLeftRatioBase,
                                connectionReversePDF,
                                forwardPreviousPDF)
                            : 0.0;
                        float rightRatioSum = bReverseHasContinuousEvent
                            ? ExtendPowerStrategyRatioSum(
                                reverseRightRatioBase,
                                connectionForwardPDF,
                                reversePreviousPDF)
                            : 0.0;
                        float splitMISWeight = rcp(1.0 + leftRatioSum + rightRatioSum);

                        result += forwardBeta * connection * reverseBeta * splitMISWeight;
                    }
                }

                float3 reverseWo = -reverseW;
#if PT_VALIDATION
                ++audit.reverseContinuousEvents;
#endif
                Layered::LayerEvent reverseEvent = LayerComposite::SampleLayerEvent(
                    reverseMaterial,
                    reverseWo,
                    reverseEtaAbove,
                    reverseEtaBelow,
                    PT_TRANSPORT_IMPORTANCE,
                    reverseRng);
                if (reverseEvent.valid == 0u || reverseEvent.isDelta != 0u)
                    break;

                float reverseEventPDF = LayerComposite::BoundaryMarginalPDF(
                    reverseMaterial,
                    reverseWo,
                    reverseEvent.wi,
                    reverseEtaAbove,
                    reverseEtaBelow);
                float forwardEventPDF = LayerComposite::BoundaryMarginalPDF(
                    reverseMaterial,
                    reverseEvent.wi,
                    reverseWo,
                    reverseEtaAbove,
                    reverseEtaBelow);
                if (!IsPathFinite(reverseEventPDF) ||
                    !IsPathFinite(forwardEventPDF) ||
                    reverseEventPDF <= 0.0 ||
                    forwardEventPDF <= 0.0)
                {
                    break;
                }

                if (bReverseHasContinuousEvent)
                {
                    reverseRightRatioBase = ExtendPowerStrategyRatioSum(
                        reverseRightRatioBase,
                        forwardEventPDF,
                        reversePreviousPDF);
                }
                else
                {
                    bReverseHasContinuousEvent = true;
                }
                reversePreviousPDF = reverseEventPDF;


                reverseW     = reverseEvent.wi;
                reverseBeta *= reverseEvent.weight;
                if (!IsPathFinite3(reverseBeta) ||
                    !any(reverseBeta > 0.0) ||
                    abs(reverseW.z) <= EPSILON_MIN)
                {
                    break;
                }

                int nextBoundary = reverseBoundary + (reverseW.z < 0.0 ? 1 : -1);
                if (nextBoundary < 0 || nextBoundary >= count)
                    break;

                int slabIndex = min(reverseBoundary, nextBoundary);
                MaterialSlabData medium = Slabs[offset + slabIndex];
                float distance = medium.thickness / max(abs(reverseW.z), EPSILON_MIN);
                float3 sigmaA = float3(medium.sigmaA_r, medium.sigmaA_g, medium.sigmaA_b);
                reverseBeta *= exp(-sigmaA * distance);
                if (!IsPathFinite3(reverseBeta) || !any(reverseBeta > 0.0))
                    break;

                reverseBoundary = nextBoundary;
                ++reverseDepth;
                if (reverseDepth >= DIRECTIONAL_RR_START_DEPTH)
                {
                    if (NextFloat(reverseRng) >= DIRECTIONAL_RR_SURVIVAL)
                        break;
                    reverseBeta /= DIRECTIONAL_RR_SURVIVAL;
                }
            }
        }

        // A path containing any delta event has no ordinary connection at that
        // vertex.  Its unique estimator samples the delta-only suffix from wi
        // and connects at the first continuous boundary.
        {
            RngState deltaRng = InitDirectionalQueryRng(
                querySeed,
                wo,
                wi,
                offset,
                uint(count),
                EVALUATE_QUERY_SALT,
                0x20000000u + forwardDepth);

            int    reverseBoundary = exitBoundary;
            float3 reverseW        = -wi;
            float3 reverseBeta     = float3(1.0, 1.0, 1.0);
            bool   bHasReverseDelta = false;
            uint   reverseDepth     = 0u;

            [loop]
            for (;;)
            {
                SurfaceMaterial reverseMaterial;
                float reverseEtaAbove;
                float reverseEtaBelow;
                if (!LoadBoundaryData(
                        rootMaterial,
                        uv,
                        ddxUV,
                        ddyUV,
                        iorExterior,
                        reverseBoundary,
                        offset,
                        reverseMaterial,
                        reverseEtaAbove,
                        reverseEtaBelow))
                {
                    return zero;
                }

                if (reverseBoundary == forwardBoundary &&
                    (forwardDepth != 0u || reverseDepth != 0u))
                {
                    float3 connectionWo = -forwardW;
                    float3 connectionWi = -reverseW;

                    PathContribution connectionLobes = LayerComposite::EvaluateBoundaryLobes(
                        reverseMaterial,
                        connectionWo,
                        connectionWi,
                        reverseEtaAbove,
                        reverseEtaBelow);
                    float3 connection =
                        connectionLobes.diffuse +
                        connectionLobes.specular +
                        connectionLobes.transmission;

                    float connectionForwardPDF = LayerComposite::BoundaryMarginalPDF(
                        reverseMaterial,
                        connectionWo,
                        connectionWi,
                        reverseEtaAbove,
                        reverseEtaBelow);
                    float connectionReversePDF = LayerComposite::BoundaryMarginalPDF(
                        reverseMaterial,
                        connectionWi,
                        connectionWo,
                        reverseEtaAbove,
                        reverseEtaBelow);

                    bool bContinuousMISOwns =
                        bForwardMISCompatible &&
                        !bHasReverseDelta &&
                        IsPathFinite3(connection) &&
                        any(connection > 0.0) &&
                        IsPathFinite(connectionForwardPDF) &&
                        IsPathFinite(connectionReversePDF) &&
                        connectionForwardPDF > 0.0 &&
                        connectionReversePDF > 0.0;

                    if (!bContinuousMISOwns)
                        result += forwardBeta * connection * reverseBeta;
                }

#if PT_VALIDATION
                ++audit.reverseDeltaEvents;
#endif
                Layered::LayerEvent reverseEvent = LayerComposite::SampleLayerEvent(
                    reverseMaterial,
                    -reverseW,
                    reverseEtaAbove,
                    reverseEtaBelow,
                    PT_TRANSPORT_IMPORTANCE,
                    deltaRng);
                if (reverseEvent.valid == 0u || reverseEvent.isDelta == 0u)
                    break;

                bHasReverseDelta = true;


                reverseW     = reverseEvent.wi;
                reverseBeta *= reverseEvent.weight;
                if (!IsPathFinite3(reverseBeta) ||
                    !any(reverseBeta > 0.0) ||
                    abs(reverseW.z) <= EPSILON_MIN)
                {
                    break;
                }

                int nextBoundary = reverseBoundary + (reverseW.z < 0.0 ? 1 : -1);
                if (nextBoundary < 0 || nextBoundary >= count)
                    break;

                int slabIndex = min(reverseBoundary, nextBoundary);
                MaterialSlabData medium = Slabs[offset + slabIndex];
                float distance = medium.thickness / max(abs(reverseW.z), EPSILON_MIN);
                float3 sigmaA = float3(medium.sigmaA_r, medium.sigmaA_g, medium.sigmaA_b);
                reverseBeta *= exp(-sigmaA * distance);
                if (!IsPathFinite3(reverseBeta) || !any(reverseBeta > 0.0))
                    break;

                reverseBoundary = nextBoundary;
                ++reverseDepth;
                if (reverseDepth >= DIRECTIONAL_RR_START_DEPTH)
                {
                    if (NextFloat(deltaRng) >= DIRECTIONAL_RR_SURVIVAL)
                        break;
                    reverseBeta /= DIRECTIONAL_RR_SURVIVAL;
                }
            }
        }

        SurfaceMaterial forwardMaterial;
        float forwardEtaAbove;
        float forwardEtaBelow;
        if (!LoadBoundaryData(
                rootMaterial,
                uv,
                ddxUV,
                ddyUV,
                iorExterior,
                forwardBoundary,
                offset,
                forwardMaterial,
                forwardEtaAbove,
                forwardEtaBelow))
        {
            return zero;
        }

        float3 forwardWo = -forwardW;
#if PT_VALIDATION
        ++audit.forwardEvents;
#endif
        Layered::LayerEvent forwardEvent = LayerComposite::SampleLayerEvent(
            forwardMaterial,
            forwardWo,
            forwardEtaAbove,
            forwardEtaBelow,
            PT_TRANSPORT_RADIANCE,
            forwardRng);
        if (forwardEvent.valid == 0u)
            break;

        if (bForwardMISCompatible)
        {
            if (forwardEvent.isDelta != 0u)
            {
                bForwardMISCompatible = false;
            }
            else
            {
                float forwardEventPDF = LayerComposite::BoundaryMarginalPDF(
                    forwardMaterial,
                    forwardWo,
                    forwardEvent.wi,
                    forwardEtaAbove,
                    forwardEtaBelow);
                float reverseEventPDF = LayerComposite::BoundaryMarginalPDF(
                    forwardMaterial,
                    forwardEvent.wi,
                    forwardWo,
                    forwardEtaAbove,
                    forwardEtaBelow);

                if (!IsPathFinite(forwardEventPDF) ||
                    !IsPathFinite(reverseEventPDF) ||
                    forwardEventPDF <= 0.0 ||
                    reverseEventPDF <= 0.0)
                {
                    bForwardMISCompatible = false;
                }
                else
                {
                    if (bForwardHasContinuousEvent)
                    {
                        forwardLeftRatioBase = ExtendPowerStrategyRatioSum(
                            forwardLeftRatioBase,
                            reverseEventPDF,
                            forwardPreviousPDF);
                    }
                    else
                    {
                        bForwardHasContinuousEvent = true;
                    }
                    forwardPreviousPDF = forwardEventPDF;
                }
            }
        }


        forwardW     = forwardEvent.wi;
        forwardBeta *= forwardEvent.weight;
        if (!IsPathFinite3(forwardBeta) ||
            !any(forwardBeta > 0.0) ||
            abs(forwardW.z) <= EPSILON_MIN)
        {
            break;
        }

        int nextBoundary = forwardBoundary + (forwardW.z < 0.0 ? 1 : -1);
        if (nextBoundary < 0 || nextBoundary >= count)
            break;

        int slabIndex = min(forwardBoundary, nextBoundary);
        MaterialSlabData medium = Slabs[offset + slabIndex];
        float distance = medium.thickness / max(abs(forwardW.z), EPSILON_MIN);
        float3 sigmaA = float3(medium.sigmaA_r, medium.sigmaA_g, medium.sigmaA_b);
        forwardBeta *= exp(-sigmaA * distance);
        if (!IsPathFinite3(forwardBeta) || !any(forwardBeta > 0.0))
            break;

        forwardBoundary = nextBoundary;
        ++forwardDepth;
        if (forwardDepth >= DIRECTIONAL_RR_START_DEPTH)
        {
            if (NextFloat(forwardRng) >= DIRECTIONAL_RR_SURVIVAL)
                break;
            forwardBeta /= DIRECTIONAL_RR_SURVIVAL;
        }
    }

    return IsPathFinite3(result) && all(result >= 0.0) ? result : zero;
}

float MarginalPDF(
    SurfaceMaterial rootMaterial,
    float2 uv,
    float2 ddxUV,
    float2 ddyUV,
    float3 wo,
    float3 wi,
    BoundaryMediumPair boundaryPair,
    uint querySeed
#if PT_VALIDATION
    , out MarginalPDFAudit audit
#endif
)
{
#if PT_VALIDATION
    audit = (MarginalPDFAudit)0;
    audit.state = PT_MARGINAL_STATE_INVALID;
#endif

    float iorExterior = boundaryPair.isEntering != 0u ? boundaryPair.mediumI.ior : boundaryPair.mediumT.ior;
    if (!IsPathFinite3(wo) || !IsPathFinite3(wi) || abs(wo.z) <= EPSILON_MIN || abs(wi.z) <= EPSILON_MIN || !IsPathFinite(iorExterior) || iorExterior <= 0.0)
        return 0.0;

    if (IsThinWalled(rootMaterial))
        return 0.0;

    if (!LayerComposite::IsSupportedThinCompositeLayout(rootMaterial))
        return 0.0;

    int  count  = int(max(rootMaterial.layerCount, 1u));
    uint offset = rootMaterial.layerOffset;
    float incidentFrameSign = LayerComposite::GetIncidentFrameSign(wo, boundaryPair.isEntering);
    wo *= incidentFrameSign;
    wi *= incidentFrameSign;

    int entryBoundary = wo.z > 0.0 ? 0 : count - 1;
    int exitBoundary  = wi.z > 0.0 ? 0 : count - 1;
    if (count == 1)
    {
        SurfaceMaterial directMaterial;
        float ior1;
        float ior2;
        if (!LoadBoundaryData(rootMaterial, uv, ddxUV, ddyUV, iorExterior, entryBoundary, offset, directMaterial, ior1, ior2))
            return 0.0;

        float result = LayerComposite::BoundaryMarginalPDF(directMaterial, wo, wi, ior1, ior2);
#if PT_VALIDATION
        audit.state = IsPathFinite(result) && result >= 0.0
            ? PT_MARGINAL_STATE_SINGLE_LAYER
            : PT_MARGINAL_STATE_NONFINITE;
#endif
        return IsPathFinite(result) && result >= 0.0 ? result : 0.0;
    }

    bool   hasContinuousProposal = false;
    int    probeBoundary         = entryBoundary;
    float3 probeW                = -wo;

    [loop]
    for (;;)
    {
#if PT_VALIDATION
        ++audit.walker.supportProbeEvents;
#endif
        SurfaceMaterial probeMaterial;
        float probeEtaAbove;
        float probeEtaBelow;
        if (!LoadBoundaryData(
                rootMaterial,
                uv,
                ddxUV,
                ddyUV,
                iorExterior,
                probeBoundary,
                offset,
                probeMaterial,
                probeEtaAbove,
                probeEtaBelow))
        {
            return 0.0;
        }

        Layered::DielectricFrame frame = LayerComposite::ResolveDielectricFrame(
            probeMaterial,
            -probeW,
            probeEtaAbove,
            probeEtaBelow);
        float3 woLayer = BxDF::RotateXY(frame.wo, -GetAnisotropyRotation(probeMaterial));
        LayerComposite::ModelMixture mixture =
            LayerComposite::ResolveModelMixture(probeMaterial, woLayer, frame.etaTOverI);

        bool isRoughSolidDielectric = probeMaterial.isSmooth == 0u && abs(frame.etaTOverI - 1.0) > EPSILON_MIN;
        bool hasRoughConductor      = probeMaterial.isSmooth == 0u && mixture.conductorPMF > 0.0;
        bool hasRoughDielectric     = isRoughSolidDielectric && mixture.dielectricPMF > 0.0;
        hasContinuousProposal =
            mixture.diffusePMF > 0.0 ||
            mixture.sheenPMF > 0.0 ||
            mixture.clearcoatPMF > 0.0 ||
            hasRoughConductor ||
            hasRoughDielectric;
        if (hasContinuousProposal)
            break;

        bool hasDeltaTransmission = mixture.dielectricPMF > 0.0 &&
                                    max3(LayerComposite::ResolveDielectricTransmissionScale(probeMaterial)) > PT_LOBE_EPS &&
                                    (probeMaterial.isSmooth != 0u || abs(frame.etaTOverI - 1.0) <= EPSILON_MIN);
        if (!hasDeltaTransmission)
            break;

        float3 wiLayer;
        float resolvedEtaTOverI;
        if (!BxDF::Lobe::Transmission::Refract(woLayer, float3(0.0, 0.0, 1.0), frame.etaTOverI, wiLayer, resolvedEtaTOverI))
            break;

        float3 wiIncident = BxDF::RotateXY(wiLayer, GetAnisotropyRotation(probeMaterial));
        probeW = frame.bFlipped != 0u ? -wiIncident : wiIncident;

        int nextBoundary = probeBoundary + (probeW.z < 0.0 ? 1 : -1);
        if (nextBoundary < 0 || nextBoundary >= count)
            break;
        probeBoundary = nextBoundary;
    }

    // Atomic direction mass is not an ordinary sr^-1 density.
    if (!hasContinuousProposal)
    {
#if PT_VALIDATION
        audit.state = PT_MARGINAL_STATE_NO_CONTINUOUS_PROPOSAL;
#endif
        return 0.0;
    }

    SurfaceMaterial exitMaterial;
    float exitEtaAbove;
    float exitEtaBelow;
    if (!LoadBoundaryData(
            rootMaterial,
            uv,
            ddxUV,
            ddyUV,
            iorExterior,
            exitBoundary,
            offset,
            exitMaterial,
            exitEtaAbove,
            exitEtaBelow))
    {
        return 0.0;
    }

    float result = 0.0;
    if (entryBoundary == exitBoundary)
    {
        result += LayerComposite::BoundaryMarginalPDF(
            exitMaterial,
            wo,
            wi,
            exitEtaAbove,
            exitEtaBelow);
    }

    RngState forwardRng = InitDirectionalQueryRng(
        querySeed,
        wo,
        wi,
        offset,
        uint(count),
        PDF_QUERY_SALT,
        0u);

    int    forwardBoundary            = entryBoundary;
    float3 forwardW                   = -wo;
    float  forwardRRWeight            = 1.0;
    bool   bForwardMISCompatible      = true;
    bool   bForwardHasContinuousEvent = false;
    float  forwardLeftRatioBase       = 0.0;
    float  forwardPreviousPDF         = 0.0;
    uint   forwardDepth               = 0u;

    [loop]
    for (;;)
    {
        // This estimates the RR-free direction-density proxy. Query roulette
        // is only an integration device and is divided out on both subpaths.
        if (bForwardMISCompatible)
        {
            RngState reverseRng = InitDirectionalQueryRng(
                querySeed,
                wo,
                wi,
                offset,
                uint(count),
                PDF_QUERY_SALT,
                0x10000000u + forwardDepth);

            int    reverseBoundary             = exitBoundary;
            float3 reverseW                    = -wi;
            float  reverseRRWeight             = 1.0;
            float  reverseLog2DensityScale     = 0.0;
            bool   bReverseHasContinuousEvent  = false;
            float  reverseRightRatioBase       = 0.0;
            float  reversePreviousPDF          = 0.0;
            uint   reverseDepth                = 0u;

            [loop]
            for (;;)
            {
                SurfaceMaterial reverseMaterial;
                float reverseEtaAbove;
                float reverseEtaBelow;
                if (!LoadBoundaryData(
                        rootMaterial,
                        uv,
                        ddxUV,
                        ddyUV,
                        iorExterior,
                        reverseBoundary,
                        offset,
                        reverseMaterial,
                        reverseEtaAbove,
                        reverseEtaBelow))
                {
                    return 0.0;
                }

                if (reverseBoundary == forwardBoundary &&
                    (forwardDepth != 0u || reverseDepth != 0u))
                {
                    float3 connectionWo = -forwardW;
                    float3 connectionWi = -reverseW;

                    float connectionForwardPDF = LayerComposite::BoundaryMarginalPDF(
                        reverseMaterial,
                        connectionWo,
                        connectionWi,
                        reverseEtaAbove,
                        reverseEtaBelow);
                    float connectionReversePDF = LayerComposite::BoundaryMarginalPDF(
                        reverseMaterial,
                        connectionWi,
                        connectionWo,
                        reverseEtaAbove,
                        reverseEtaBelow);

                    bool bConnectionSupported =
                        IsPathFinite(connectionForwardPDF) &&
                        IsPathFinite(connectionReversePDF) &&
                        connectionForwardPDF > 0.0 &&
                        connectionReversePDF > 0.0;

                    if (bConnectionSupported)
                    {
                        float leftRatioSum = bForwardHasContinuousEvent
                            ? ExtendPowerStrategyRatioSum(
                                forwardLeftRatioBase,
                                connectionReversePDF,
                                forwardPreviousPDF)
                            : 0.0;
                        float rightRatioSum = bReverseHasContinuousEvent
                            ? ExtendPowerStrategyRatioSum(
                                reverseRightRatioBase,
                                connectionForwardPDF,
                                reversePreviousPDF)
                            : 0.0;
                        float splitMISWeight = rcp(1.0 + leftRatioSum + rightRatioSum);

                        float log2Contribution =
                            log2(max(forwardRRWeight, 1.0e-30)) +
                            log2(connectionForwardPDF) +
                            log2(max(reverseRRWeight, 1.0e-30)) +
                            reverseLog2DensityScale +
                            log2(splitMISWeight);
                        result += exp2(clamp(log2Contribution, -100.0, 100.0));
                    }
                }

                float3 reverseWo = -reverseW;
#if PT_VALIDATION
                ++audit.walker.reverseContinuousEvents;
#endif
                Layered::LayerEvent reverseEvent = LayerComposite::SampleLayerEvent(
                    reverseMaterial,
                    reverseWo,
                    reverseEtaAbove,
                    reverseEtaBelow,
                    PT_TRANSPORT_IMPORTANCE,
                    reverseRng);
                if (reverseEvent.valid == 0u || reverseEvent.isDelta != 0u)
                    break;

                float reverseEventPDF = LayerComposite::BoundaryMarginalPDF(
                    reverseMaterial,
                    reverseWo,
                    reverseEvent.wi,
                    reverseEtaAbove,
                    reverseEtaBelow);
                float forwardEventPDF = LayerComposite::BoundaryMarginalPDF(
                    reverseMaterial,
                    reverseEvent.wi,
                    reverseWo,
                    reverseEtaAbove,
                    reverseEtaBelow);
                if (!IsPathFinite(reverseEventPDF) ||
                    !IsPathFinite(forwardEventPDF) ||
                    reverseEventPDF <= 0.0 ||
                    forwardEventPDF <= 0.0)
                {
                    break;
                }

                reverseLog2DensityScale +=
                    log2(forwardEventPDF) -
                    log2(reverseEventPDF);

                if (bReverseHasContinuousEvent)
                {
                    reverseRightRatioBase = ExtendPowerStrategyRatioSum(
                        reverseRightRatioBase,
                        forwardEventPDF,
                        reversePreviousPDF);
                }
                else
                {
                    bReverseHasContinuousEvent = true;
                }
                reversePreviousPDF = reverseEventPDF;


                reverseW = reverseEvent.wi;
                if (!IsPathFinite3(reverseW) || abs(reverseW.z) <= EPSILON_MIN)
                    break;

                int nextBoundary = reverseBoundary + (reverseW.z < 0.0 ? 1 : -1);
                if (nextBoundary < 0 || nextBoundary >= count)
                    break;

                reverseBoundary = nextBoundary;
                ++reverseDepth;
                if (reverseDepth >= DIRECTIONAL_RR_START_DEPTH)
                {
                    if (NextFloat(reverseRng) >= DIRECTIONAL_RR_SURVIVAL)
                        break;
                    reverseRRWeight /= DIRECTIONAL_RR_SURVIVAL;
                }
            }
        }

        {
            RngState deltaRng = InitDirectionalQueryRng(
                querySeed,
                wo,
                wi,
                offset,
                uint(count),
                PDF_QUERY_SALT,
                0x20000000u + forwardDepth);

            int    reverseBoundary         = exitBoundary;
            float3 reverseW                = -wi;
            float  reverseRRWeight         = 1.0;
            float  reverseLog2DensityScale = 0.0;
            bool   bHasReverseDelta        = false;
            uint   reverseDepth            = 0u;

            [loop]
            for (;;)
            {
                SurfaceMaterial reverseMaterial;
                float reverseEtaAbove;
                float reverseEtaBelow;
                if (!LoadBoundaryData(
                        rootMaterial,
                        uv,
                        ddxUV,
                        ddyUV,
                        iorExterior,
                        reverseBoundary,
                        offset,
                        reverseMaterial,
                        reverseEtaAbove,
                        reverseEtaBelow))
                {
                    return 0.0;
                }

                if (reverseBoundary == forwardBoundary &&
                    (forwardDepth != 0u || reverseDepth != 0u))
                {
                    float3 connectionWo = -forwardW;
                    float3 connectionWi = -reverseW;

                    float connectionForwardPDF = LayerComposite::BoundaryMarginalPDF(
                        reverseMaterial,
                        connectionWo,
                        connectionWi,
                        reverseEtaAbove,
                        reverseEtaBelow);
                    float connectionReversePDF = LayerComposite::BoundaryMarginalPDF(
                        reverseMaterial,
                        connectionWi,
                        connectionWo,
                        reverseEtaAbove,
                        reverseEtaBelow);

                    bool bContinuousMISOwns =
                        bForwardMISCompatible &&
                        !bHasReverseDelta &&
                        IsPathFinite(connectionForwardPDF) &&
                        IsPathFinite(connectionReversePDF) &&
                        connectionForwardPDF > 0.0 &&
                        connectionReversePDF > 0.0;

                    if (!bContinuousMISOwns &&
                        IsPathFinite(connectionForwardPDF) &&
                        connectionForwardPDF > 0.0)
                    {
                        float log2Contribution =
                            log2(max(forwardRRWeight, 1.0e-30)) +
                            log2(connectionForwardPDF) +
                            log2(max(reverseRRWeight, 1.0e-30)) +
                            reverseLog2DensityScale;
                        result += exp2(clamp(log2Contribution, -100.0, 100.0));
                    }
                }

                float3 reverseWo = -reverseW;
#if PT_VALIDATION
                ++audit.walker.reverseDeltaEvents;
#endif
                Layered::LayerEvent reverseEvent = LayerComposite::SampleLayerEvent(
                    reverseMaterial,
                    reverseWo,
                    reverseEtaAbove,
                    reverseEtaBelow,
                    PT_TRANSPORT_IMPORTANCE,
                    deltaRng);
                if (reverseEvent.valid == 0u || reverseEvent.isDelta == 0u)
                    break;

                bHasReverseDelta = true;

                float reverseDeltaPMF = LayerComposite::BoundaryMarginalDeltaPMF(
                    reverseMaterial,
                    reverseWo,
                    reverseEvent.wi,
                    reverseEtaAbove,
                    reverseEtaBelow);
                float forwardDeltaPMF = LayerComposite::BoundaryMarginalDeltaPMF(
                    reverseMaterial,
                    reverseEvent.wi,
                    reverseWo,
                    reverseEtaAbove,
                    reverseEtaBelow);
                if (!IsPathFinite(reverseDeltaPMF) ||
                    !IsPathFinite(forwardDeltaPMF) ||
                    reverseDeltaPMF <= 0.0 ||
                    forwardDeltaPMF <= 0.0)
                {
                    break;
                }
                reverseLog2DensityScale +=
                    log2(forwardDeltaPMF) - log2(reverseDeltaPMF);


                if (reverseEvent.isTransmission != 0u)
                {
                    // Current closures have symmetric delta masses. Refraction
                    // still changes the directional measure by this Jacobian.
                    float jacobian = BxDF::AbsCosTheta(reverseWo) /
                        max(sq(reverseEvent.etaTOverI) * BxDF::AbsCosTheta(reverseEvent.wi), EPSILON_MIN);
                    if (!IsPathFinite(jacobian) || jacobian <= 0.0)
                        break;
                    reverseLog2DensityScale += log2(jacobian);
                }

                reverseW = reverseEvent.wi;
                if (!IsPathFinite3(reverseW) || abs(reverseW.z) <= EPSILON_MIN)
                    break;

                int nextBoundary = reverseBoundary + (reverseW.z < 0.0 ? 1 : -1);
                if (nextBoundary < 0 || nextBoundary >= count)
                    break;

                reverseBoundary = nextBoundary;
                ++reverseDepth;
                if (reverseDepth >= DIRECTIONAL_RR_START_DEPTH)
                {
                    if (NextFloat(deltaRng) >= DIRECTIONAL_RR_SURVIVAL)
                        break;
                    reverseRRWeight /= DIRECTIONAL_RR_SURVIVAL;
                }
            }
        }

        SurfaceMaterial forwardMaterial;
        float forwardEtaAbove;
        float forwardEtaBelow;
        if (!LoadBoundaryData(
                rootMaterial,
                uv,
                ddxUV,
                ddyUV,
                iorExterior,
                forwardBoundary,
                offset,
                forwardMaterial,
                forwardEtaAbove,
                forwardEtaBelow))
        {
            return 0.0;
        }

        float3 forwardWo = -forwardW;
#if PT_VALIDATION
        ++audit.walker.forwardEvents;
#endif
        Layered::LayerEvent forwardEvent = LayerComposite::SampleLayerEvent(
            forwardMaterial,
            forwardWo,
            forwardEtaAbove,
            forwardEtaBelow,
            PT_TRANSPORT_RADIANCE,
            forwardRng);
        if (forwardEvent.valid == 0u)
            break;

        if (bForwardMISCompatible)
        {
            if (forwardEvent.isDelta != 0u)
            {
                bForwardMISCompatible = false;
            }
            else
            {
                float forwardEventPDF = LayerComposite::BoundaryMarginalPDF(
                    forwardMaterial,
                    forwardWo,
                    forwardEvent.wi,
                    forwardEtaAbove,
                    forwardEtaBelow);
                float reverseEventPDF = LayerComposite::BoundaryMarginalPDF(
                    forwardMaterial,
                    forwardEvent.wi,
                    forwardWo,
                    forwardEtaAbove,
                    forwardEtaBelow);

                if (!IsPathFinite(forwardEventPDF) ||
                    !IsPathFinite(reverseEventPDF) ||
                    forwardEventPDF <= 0.0 ||
                    reverseEventPDF <= 0.0)
                {
                    bForwardMISCompatible = false;
                }
                else
                {
                    if (bForwardHasContinuousEvent)
                    {
                        forwardLeftRatioBase = ExtendPowerStrategyRatioSum(
                            forwardLeftRatioBase,
                            reverseEventPDF,
                            forwardPreviousPDF);
                    }
                    else
                    {
                        bForwardHasContinuousEvent = true;
                    }
                    forwardPreviousPDF = forwardEventPDF;
                }
            }
        }


        forwardW = forwardEvent.wi;
        if (!IsPathFinite3(forwardW) || abs(forwardW.z) <= EPSILON_MIN)
            break;

        int nextBoundary = forwardBoundary + (forwardW.z < 0.0 ? 1 : -1);
        if (nextBoundary < 0 || nextBoundary >= count)
            break;

        forwardBoundary = nextBoundary;
        ++forwardDepth;
        if (forwardDepth >= DIRECTIONAL_RR_START_DEPTH)
        {
            if (NextFloat(forwardRng) >= DIRECTIONAL_RR_SURVIVAL)
                break;
            forwardRRWeight /= DIRECTIONAL_RR_SURVIVAL;
        }
    }

    if (!IsPathFinite(result) || result < 0.0)
    {
#if PT_VALIDATION
        audit.state = PT_MARGINAL_STATE_NONFINITE;
#endif
        return 0.0;
    }

#if PT_VALIDATION
    audit.state = result > 0.0
        ? PT_MARGINAL_STATE_NLAYER_POSITIVE
        : PT_MARGINAL_STATE_NLAYER_ZERO_CANDIDATE;
#endif
    return result;
}
} // namespace DirectionalComposite

} // namespace BxDF

#endif // _HLSL_PATHCOMPOSITE_HEADER
