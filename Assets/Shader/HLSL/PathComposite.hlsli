#ifndef _HLSL_PATHCOMPOSITE_HEADER
#define _HLSL_PATHCOMPOSITE_HEADER

#include "PathLayered.hlsli"
#include "Sampling.hlsli"

namespace BxDF
{

namespace LayerComposite
{

static const uint LOBE_SLOT_DIFFUSE      = 0u;
static const uint LOBE_SLOT_SPECULAR     = 1u;
static const uint LOBE_SLOT_CLEARCOAT    = 2u;
static const uint LOBE_SLOT_TRANSMISSION = 3u;
static const uint LOBE_SLOT_COUNT        = 4u;

// Keep both rough dielectric branches away from zero without discarding Fresnel importance sampling.
static const float ROUGH_DIELECTRIC_PROPOSAL_SAFETY_MIX = 0.2;

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

bool RefractConeBoundary(float2 incidentDir, float2 normal, float etaInv, out float2 transmittedDir)
{
    float NoI = dot(normal, incidentDir);
    float k = 1.0 - sq(etaInv) * (1.0 - sq(NoI));

    if (k < -EPSILON_MIN)
    {
        transmittedDir = incidentDir - normal * NoI;
        float tangentLenSq = dot(transmittedDir, transmittedDir);
        if (!IsPathFinite(tangentLenSq) || tangentLenSq <= EPSILON_MIN)
            return false;

        transmittedDir *= rsqrt(tangentLenSq);
        return IsPathFinite(transmittedDir.x) && IsPathFinite(transmittedDir.y);
    }
    transmittedDir = etaInv * incidentDir - normal * (etaInv * NoI + safeSqrt(max(k, 0.0)));

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
        !IsPathFinite(event.eta) || event.eta <= EPSILON_MIN ||
        !IsPathFinite(cone.radius) || !IsPathFinite(cone.tanHalfAngle))
    {
        return false;
    }

    if (abs(event.eta - 1.0) <= EPSILON_MIN)
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

        float  etaP;
        float3 whIncident = BxDF::Transmission::HalfVector(woIncident, wiIncident, event.eta, etaP);
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
    float etaIOverT = rcp(event.eta); // LayerEvent stores etaT / etaI.
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
    if (event.lobe == BxDF::LOBE_DIFFUSE)
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

struct LobeMixture
{
    // diffuse, specular reflection, clearcoat, transmission
    float4 pmf; // sampling probability
};

float4 NormalizeLobePMF(float4 weights)
{
    float weightSum = weights.x + weights.y + weights.z + weights.w;
    if (weightSum <= EPSILON_MIN)
        return float4(1.0, 0.0, 0.0, 0.0);
    return weights / weightSum;
}

float DielectricF0(float eta)
{
    eta = max(eta, 1.0e-4);
    float f0 = (eta - 1.0) / (eta + 1.0);
    return f0 * f0;
}

float DielectricSpecularScale(SurfaceMaterial sm, float eta)
{
    if (!IsPrincipledMaterial(sm) && !HasDielectricSpecularLobe(sm, eta))
        return 0.0;

    float strength = saturate(sm.specularStrength);
    if (!IsPrincipledMaterial(sm))
        return strength * max3(saturate(sm.specularColor));

    float f0 = DielectricF0(eta);
    float3 tint = f0 > 1.0e-6
        ? max(sm.specularColor / f0, float3(0.0, 0.0, 0.0))
        : float3(1.0, 1.0, 1.0);
    return strength * max3(tint);
}

float ConductorReflectionProposalWeight(SurfaceMaterial sm, float3 wo)
{
    float metallic = saturate(sm.metallic);
    if (metallic <= PT_LOBE_EPS)
        return 0.0;

    float3 F = IsPrincipledMaterial(sm)
        ? saturate(sm.specularStrength) * BxDF::Fresnel::Schlick(saturate(sm.albedo), BxDF::AbsCosTheta(wo))
        : BxDF::Fresnel::Schlick(saturate(sm.specularColor), BxDF::AbsCosTheta(wo));
    return metallic * max3(max(F, float3(0.0, 0.0, 0.0)));
}


// pi_k: 'proposal' PMF
LobeMixture ResolveLobeMixture(SurfaceMaterial sm, float eta, float3 wo)
{
    LobeMixture ls;
    ls.pmf = float4(0.0, 0.0, 0.0, 0.0);

    float metallic     = saturate(sm.metallic);
    float dielectric   = 1.0 - metallic;
    float transmission = saturate(sm.transmission);

    float opaqueDielectric = dielectric * (1.0 - transmission);
    float wSheen           = SheenSamplingWeight(sm);
    float wDiffuse         = max(opaqueDielectric, wSheen);

    bool isSymmetricDeltaProposal = sm.isSmooth != 0u && dielectric * transmission > 0.0;
    float3 proposalWo = isSymmetricDeltaProposal ? float3(0.0, 0.0, 1.0) : wo;

    float dielectricFresnel = BxDF::Fresnel::Dielectric(BxDF::CosTheta(proposalWo), 1.0, eta);
    float dielectricScale = DielectricSpecularScale(sm, eta);
    bool hasDielectricReflection = dielectricScale > PT_LOBE_EPS;
    bool hasTransmissiveDielectric =
        transmission > PT_LOBE_EPS &&
        dielectric > PT_LOBE_EPS;

    float dielectricReflectionProposal = dielectricFresnel * dielectricScale;
    float transmissionFresnelProxy = 1.0 - dielectricFresnel;
    if (hasTransmissiveDielectric)
    {
        if (sm.isSmooth != 0u)
        {
            if (hasDielectricReflection)
                dielectricReflectionProposal = 1.0;
            transmissionFresnelProxy = 1.0;
        }
        else
        {
            if (hasDielectricReflection)
                dielectricReflectionProposal = lerp(dielectricReflectionProposal, 0.5, ROUGH_DIELECTRIC_PROPOSAL_SAFETY_MIX);
            transmissionFresnelProxy = lerp(transmissionFresnelProxy, 0.5, ROUGH_DIELECTRIC_PROPOSAL_SAFETY_MIX);
        }
    }

    float wSpecular =
        ConductorReflectionProposalWeight(sm, proposalWo) +
        dielectric * dielectricReflectionProposal;
    float wClearcoat = IsPrincipledMaterial(sm) ? saturate(sm.clearcoat) * 0.25 : saturate(sm.clearcoat);
    float wTransmission = dielectric * transmission * transmissionFresnelProxy;

    if (IsSmoothConductor(sm))
    {
        ls.pmf.y = 1.0;
        return ls;
    }

    ls.pmf = NormalizeLobePMF(float4(wDiffuse, wSpecular, wClearcoat, wTransmission));
    return ls;
}


float3 EvaluateDiffuseBRDF(SurfaceMaterial sm, float3 wo, float3 wi)
{
    if (!BxDF::SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);

    float3 f = IsPrincipledMaterial(sm)
        ? BxDF::Diffuse::EvaluateBRDF(sm.albedo, sm.roughness, wo, wi)
        : BxDF::Diffuse::Lambert(sm.albedo);

    float wDiffuse = (1.0 - saturate(sm.metallic)) * (1.0 - saturate(sm.transmission));
    return f * wDiffuse;
}

float3 EvaluateSheenBRDF(SurfaceMaterial sm, float3 wo, float3 wi)
{
    if (!HasSheenLobe(sm) || !BxDF::SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);

    if (!IsPrincipledMaterial(sm))
        return BxDF::Sheen::EvaluateBRDF(sm.sheenColor, sm.sheenRoughness, wo, wi);

    float3 h = wo + wi;
    float  hLenSq = dot(h, h);
    if (hLenSq <= 0.0)
        return float3(0.0, 0.0, 0.0);

    h = h * rsqrt(hLenSq);
    float sheenWeight = pow(saturate(1.0 - dot(wi, h)), 5.0);
    return sm.sheenColor * sheenWeight;
}

float3 EvaluateSmoothSpecularWeight(SurfaceMaterial sm, float3 wo, float eta)
{
    float wConductor  = saturate(sm.metallic);
    float wDielectric = 1.0 - wConductor;

    if (!IsPrincipledMaterial(sm))
    {
        float3 f = float3(0.0, 0.0, 0.0);

        if (wConductor > PT_LOBE_EPS)
            f += wConductor * BxDF::Conductor::Smooth::EvaluateReflection(wo, sm.specularColor);

        if (wDielectric > PT_LOBE_EPS && HasDielectricSpecularLobe(sm, eta))
        {
            float3 specularScale = saturate(sm.specularColor) * saturate(sm.specularStrength);
            f += wDielectric * specularScale * BxDF::Dielectric::Smooth::EvaluateReflection(wo, eta);
        }

        return f;
    }

    // principled branch
    float cosTheta = saturate(BxDF::AbsCosTheta(wo));

    float f0Eta = (eta - 1.0) / (eta + 1.0);
    f0Eta *= f0Eta;

    float3 dielectricTint = (f0Eta > 1.0e-6) ? max(sm.specularColor / f0Eta, float3(0.0, 0.0, 0.0)) : float3(1.0, 1.0, 1.0);

    float  fd = BxDF::Fresnel::Dielectric(cosTheta, 1.0, eta);
    float3 Fdielectric = float3(fd, fd, fd);
    float3 Fconductor  = BxDF::Fresnel::Schlick(sm.albedo, cosTheta);

    return (dielectricTint * wDielectric * Fdielectric + wConductor * Fconductor) * sm.specularStrength;
}

//
float3 EvaluateSpecularBRDF(SurfaceMaterial sm, float3 wo, float3 wi, float eta)
{
    if (!BxDF::SameHemisphere(wo, wi) || sm.isSmooth != 0u)
        return float3(0.0, 0.0, 0.0);

    float wConductor  = saturate(sm.metallic);
    float wDielectric = 1.0 - wConductor;

    float2 alpha = GetAlpha2(sm);

    if (!IsPrincipledMaterial(sm))
    {
        float3 f = float3(0.0, 0.0, 0.0);
        if (wConductor > PT_LOBE_EPS)
        {
            f += wConductor * BxDF::Conductor::EvaluateReflection(wo, wi, sm.specularColor, alpha.x, alpha.y);
        }

        if (wDielectric > PT_LOBE_EPS && HasDielectricSpecularLobe(sm, eta))
        {
            float3 specularScale = saturate(sm.specularColor) * saturate(sm.specularStrength);
            f += wDielectric * specularScale * BxDF::Dielectric::EvaluateReflection(wo, wi, alpha.x, alpha.y, eta);
        }

        return f;
    }

    // principled branch
    float3 h = wo + wi;
    float  hLenSq = dot(h, h);
    if (hLenSq <= 0.0)
        return float3(0.0, 0.0, 0.0);
    h = h * rsqrt(hLenSq);

    float WoH = saturate(dot(wo, h));
    if (WoH <= 0.0)
        return float3(0.0, 0.0, 0.0);

    float f0Eta = (eta - 1.0) / (eta + 1.0);
    f0Eta *= f0Eta;

    float3 dielectricTint = (f0Eta > 1.0e-6) ? max(sm.specularColor / f0Eta, float3(0.0, 0.0, 0.0)) : float3(1.0, 1.0, 1.0);

    float  fd = BxDF::Fresnel::Dielectric(WoH, 1.0, eta);
    float3 Fdielectric = float3(fd, fd, fd);
    float3 Fconductor  = BxDF::Fresnel::Schlick(sm.albedo, WoH);

    float3 Fprincipled = (dielectricTint * wDielectric * Fdielectric + wConductor * Fconductor) * sm.specularStrength;
    return Fprincipled * BxDF::Reflection::EvaluateBRDF(wo, wi, float3(1.0, 1.0, 1.0), alpha.x, alpha.y);
}

float3 EvaluateClearcoatBRDF(SurfaceMaterial sm, float3 wo, float3 wi)
{
    if (!HasClearcoatLobe(sm) || !BxDF::SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);

    float3 clearcoatBRDF = BxDF::Clearcoat::EvaluateBRDF(wo, wi, GetClearcoatAlpha(sm));
    if (!IsPrincipledMaterial(sm))
        return saturate(sm.clearcoat) * clearcoatBRDF;

    float noV = BxDF::AbsCosTheta(wo);
    float noL = BxDF::AbsCosTheta(wi);

    // disney-principled clearcoat: BxDF keeps the physical denominator; this adapter matches Disney/Mitsuba.
    float disneyClearcoatScale = 4.0 * noV * noL;
    return (sm.clearcoat * 0.25) * disneyClearcoatScale * clearcoatBRDF;
}


// wk * fk : 'physical' weighted bsdf
PathContribution EvaluateBoundaryLobes(
    SurfaceMaterial sm,
    float3 wo,
    float3 wi,
    float etaAbove,
    float etaBelow,
    uint transportMode)
{
    Layered::DielectricFrame frame = Layered::MakeDielectricFrame(wo, etaAbove, etaBelow);

    float3 woLayer    = BxDF::RotateXY(frame.wo, -GetAnisotropyRotation(sm));
    float3 wiIncident = frame.bFlipped != 0u ? -wi : wi;
    float3 wiLayer    = BxDF::RotateXY(wiIncident, -GetAnisotropyRotation(sm));

    PathContribution lobes = ZeroPathContribution();
    lobes.diffuse  = EvaluateDiffuseBRDF(sm, woLayer, wiLayer) + EvaluateSheenBRDF(sm, woLayer, wiLayer);
    lobes.specular = EvaluateSpecularBRDF(sm, woLayer, wiLayer, frame.eta) + EvaluateClearcoatBRDF(sm, woLayer, wiLayer);

    float wTransmission = (1.0 - saturate(sm.metallic)) * saturate(sm.transmission);
    if (wTransmission > PT_LOBE_EPS && !BxDF::SameHemisphere(woLayer, wiLayer))
    {
        float2 alpha = GetAlpha2(sm);
        lobes.transmission = wTransmission * BxDF::Dielectric::EvaluateTransmission(woLayer, wiLayer, alpha.x, alpha.y, frame.eta, transportMode);
    }

    return lobes;
}

PathContribution EvaluateBoundaryLobes(SurfaceMaterial sm, float3 wo, float3 wi, float etaAbove, float etaBelow)
{
    return EvaluateBoundaryLobes(sm, wo, wi, etaAbove, etaBelow, PT_TRANSPORT_RADIANCE);
}

// 'proposal' bsdf's sampling pdf
float BoundaryMarginalPDF(SurfaceMaterial sm, float3 wo, float3 wi, float etaAbove, float etaBelow)
{
    Layered::DielectricFrame frame = Layered::MakeDielectricFrame(wo, etaAbove, etaBelow);

    float3 woLayer    = BxDF::RotateXY(frame.wo, -GetAnisotropyRotation(sm));
    float3 wiIncident = frame.bFlipped != 0u ? -wi : wi;
    float3 wiLayer    = BxDF::RotateXY(wiIncident, -GetAnisotropyRotation(sm));

    float2 alpha = GetAlpha2(sm);

    LobeMixture ls = ResolveLobeMixture(sm, frame.eta, woLayer);
    float pdf = ls.pmf.x * BxDF::Diffuse::EvaluatePDF(woLayer, wiLayer);

    if (ls.pmf.y > 0.0 && sm.isSmooth == 0u)
        pdf += ls.pmf.y * BxDF::Reflection::EvaluatePDF(woLayer, wiLayer, alpha.x, alpha.y);

    if (ls.pmf.z > 0.0)
        pdf += ls.pmf.z * BxDF::Clearcoat::EvaluatePDF(woLayer, wiLayer, GetClearcoatAlpha(sm));

    if (ls.pmf.w > 0.0 && sm.isSmooth == 0u)
        pdf += ls.pmf.w * BxDF::Transmission::EvaluatePDF(woLayer, wiLayer, alpha.x, alpha.y, frame.eta);

    return pdf;
}

uint ChooseLobeSlot(LobeMixture ls, float uc)
{
    float remainingUc = saturate(uc);

    float cumulative = ls.pmf.x;
    if (ls.pmf.x > 0.0 && remainingUc < cumulative)
    {
        return LOBE_SLOT_DIFFUSE;
    }

    cumulative += ls.pmf.y;
    if (ls.pmf.y > 0.0 && remainingUc < cumulative)
    {
        return LOBE_SLOT_SPECULAR;
    }

    cumulative += ls.pmf.z;
    if (ls.pmf.z > 0.0 && remainingUc < cumulative)
    {
        return LOBE_SLOT_CLEARCOAT;
    }

    if (ls.pmf.w > 0.0)
    {
        return LOBE_SLOT_TRANSMISSION;
    }

    return LOBE_SLOT_DIFFUSE;
}

Layered::LayerEvent SampleLayerEvent(SurfaceMaterial sm, float3 wo, float etaAbove, float etaBelow, uint transportMode, inout RngState rng)
{
    Layered::LayerEvent event = Layered::InitializeLayerEvent();
    Layered::DielectricFrame frame = Layered::MakeDielectricFrame(wo, etaAbove, etaBelow);
    float3 woLayer = BxDF::RotateXY(frame.wo, -GetAnisotropyRotation(sm));

    LobeMixture ls = ResolveLobeMixture(sm, frame.eta, woLayer);

    uint slot = ChooseLobeSlot(ls, NextFloat(rng));

    float slotPmf = ls.pmf[slot];
    if (!IsPathFinite(slotPmf) || slotPmf <= 0.0)
        return event;

    float2 u = NextFloat2(rng);

    BxDF::BSDFSample bs = (BxDF::BSDFSample)0;
    switch (slot)
    {
        case LOBE_SLOT_DIFFUSE:
        {
            bs.wi  = BxDF::Diffuse::SampleRay(woLayer, u);
            bs.pdf = BxDF::Diffuse::EvaluatePDF(woLayer, bs.wi);

            if (!IsPathFinite(bs.pdf) || bs.pdf <= 0.0)
                return event;

            float3 f = EvaluateDiffuseBRDF(sm, woLayer, bs.wi) + EvaluateSheenBRDF(sm, woLayer, bs.wi);
            bs.weight = f * BxDF::AbsCosTheta(bs.wi) / bs.pdf;

            bs.isDelta = 0u;
            bs.lobe    = BxDF::LOBE_DIFFUSE;
        }
        break;

        case LOBE_SLOT_SPECULAR:
        {
            if (sm.isSmooth != 0u)
            {
                bs.wi      = float3(-woLayer.x, -woLayer.y, woLayer.z);
                bs.pdf     = 1.0; // deterministic
                bs.weight  = EvaluateSmoothSpecularWeight(sm, woLayer, frame.eta);
                bs.lobe    = BxDF::LOBE_SPECULAR;
                bs.isDelta = 1u;
            }
            else
            {
                float2 alpha = GetAlpha2(sm);

                bs.wi  = BxDF::Reflection::SampleRay(woLayer, alpha.x, alpha.y, u);
                bs.pdf = BxDF::Reflection::EvaluatePDF(woLayer, bs.wi, alpha.x, alpha.y);

                if (!IsPathFinite(bs.pdf) || bs.pdf <= 0.0)
                    return event;

                bs.weight = EvaluateSpecularBRDF(sm, woLayer, bs.wi, frame.eta) * BxDF::AbsCosTheta(bs.wi) / bs.pdf;

                bs.isDelta = 0u;
                bs.lobe    = BxDF::LOBE_SPECULAR;
            }
        }
        break;

        case LOBE_SLOT_CLEARCOAT:
        {
            float alpha = GetClearcoatAlpha(sm);

            bs.wi  = BxDF::Clearcoat::SampleRay(woLayer, alpha, u);
            bs.pdf = BxDF::Clearcoat::EvaluatePDF(woLayer, bs.wi, alpha);

            if (!IsPathFinite(bs.pdf) || bs.pdf <= 0.0)
                return event;

            bs.weight = EvaluateClearcoatBRDF(sm, woLayer, bs.wi) * BxDF::AbsCosTheta(bs.wi) / bs.pdf;

            bs.isDelta = 0u;
            bs.lobe    = BxDF::LOBE_CLEARCOAT;
        }
        break;

        case LOBE_SLOT_TRANSMISSION:
        {
            float2 alpha = GetAlpha2(sm);
            bool isDelta = frame.eta == 1.0 || BxDF::GGX::IsSmooth(alpha.x, alpha.y);

            bs.wi = BxDF::Transmission::SampleRay(woLayer, alpha.x, alpha.y, frame.eta, u);
            if (!IsPathFinite3(bs.wi) || dot(bs.wi, bs.wi) <= EPSILON_MIN)
                return event;

            float wTransmission = (1.0 - saturate(sm.metallic)) * saturate(sm.transmission);
            if (isDelta)
            {
                bs.pdf     = 1.0; // deterministic conditional mass
                bs.isDelta = 1u;
                bs.weight = wTransmission * BxDF::Dielectric::Smooth::EvaluateTransmission(
                    woLayer,
                    bs.wi,
                    frame.eta,
                    transportMode);
            }
            else
            {
                bs.pdf     = BxDF::Transmission::EvaluatePDF(woLayer, bs.wi, alpha.x, alpha.y, frame.eta);
                bs.isDelta = 0u;
                if (!IsPathFinite(bs.pdf) || bs.pdf <= 0.0)
                    return event;

                float3 f = BxDF::Dielectric::EvaluateTransmission(
                    woLayer,
                    bs.wi,
                    alpha.x,
                    alpha.y,
                    frame.eta,
                    transportMode);
                bs.weight = wTransmission * f * BxDF::AbsCosTheta(bs.wi) / bs.pdf;
            }

            bs.lobe = BxDF::LOBE_TRANSMISSION;
        }
        break;

        default:
            return event;
    }

    if (!IsPathFinite(bs.pdf) || bs.pdf <= 0.0 ||
        !IsPathFinite3(bs.wi) || dot(bs.wi, bs.wi) <= EPSILON_MIN ||
        !IsPathFinite3(bs.weight) || any(bs.weight < 0.0))
    {
        return event;
    }

    // layer frame -> incident(or transmitted)-side frame
    float3 wiIncident = BxDF::RotateXY(bs.wi, GetAnisotropyRotation(sm));
    event.wi = frame.bFlipped != 0u ? -wiIncident : wiIncident;
    event.isDelta        = bs.isDelta;
    event.isTransmission = BxDF::SameHemisphere(woLayer, bs.wi) ? 0u : 1u;
    event.eta            = event.isTransmission != 0u ? frame.eta : 1.0;

    event.pdf    = slotPmf * bs.pdf;
    event.weight = bs.weight / slotPmf;

    if (!IsPathFinite(event.pdf) || event.pdf <= 0.0 ||
        !IsPathFinite3(event.weight) || any(event.weight < 0.0))
        return Layered::InitializeLayerEvent();

    event.lobe  = bs.lobe;
    event.flags = bs.lobe == BxDF::LOBE_DIFFUSE ? PT_BSDF_FLAG_DIFFUSE : event.isTransmission != 0u ? PT_BSDF_FLAG_TRANSMISSION : PT_BSDF_FLAG_GLOSSY;
    event.valid = 1u;
    return event;
}

PathBSDFSample SampleRay(
        SurfaceMaterial rootMaterial,
        float2 uv,
        float2 ddxUV,
        float2 ddyUV,
        float3 wo,
        float etaExterior,
        uint rrStartDepth,
        float roughnessSpreadScale,
        inout RayCone rayCone,
        inout RngState rng)
{
    PathBSDFSample s = (PathBSDFSample)0;
    s.attempted  = 1u;
    s.rrEtaScale = 1.0;

    if (!IsPathFinite3(wo) ||
        abs(wo.z) <= EPSILON_MIN ||
        !IsPathFinite(etaExterior) ||
        etaExterior <= 0.0)
        return s;

    StructuredBuffer<MaterialSlabData> Slabs = GetResource(g_MaterialSlabs.index);
    StructuredBuffer<MaterialData> Materials = GetResource(g_Materials.index);

    int  count    = int(max(rootMaterial.layerCount, 1u));
    uint offset   = rootMaterial.layerOffset;
    int  boundary = wo.z > 0.0 ? 0 : count - 1;

    if (count > 1 && offset == INVALID_INDEX)
        return s;

    float3 w = -wo;

    float3 beta       = float3(1.0, 1.0, 1.0);
    float  qH         = 1.0;
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
            if (slab.materialID == INVALID_INDEX)
                return s;

            sm = LoadSurfaceMaterial(slab.materialID, uv, ddxUV, ddyUV, rootMaterial.tangentFrameSign);
        }

        float etaAbove = etaExterior;
        if (boundary > 0)
        {
            MaterialSlabData aboveSlab = Slabs[offset + boundary - 1];
            if (aboveSlab.materialID == INVALID_INDEX)
                return s;

            etaAbove = max(Materials[aboveSlab.materialID].ior, 1.0e-4);
        }
        float etaBelow = max(sm.ior, 1.0e-4);

        Layered::LayerEvent event = SampleLayerEvent(sm, -w, etaAbove, etaBelow, PT_TRANSPORT_RADIANCE, rng);
        if (!Layered::IsLayerEventValid(event))
            return s;

        bool crossedBoundary = w.z * event.wi.z > 0.0;
        if (crossedBoundary != (event.isTransmission != 0u))
            return s;

        UpdateRayCone(sm, -w, event, roughnessSpreadScale, rayCone);

        w     = event.wi;
        qH   *= event.pdf;
        beta *= event.weight;

        allDelta     = allDelta && event.isDelta != 0u;
        historyFlags |= event.flags;

        if (event.isTransmission != 0u)
            rrEtaScale *= sq(event.eta);

        if (!IsPathFinite3(beta) || abs(w.z) <= EPSILON_MIN)
            return s;

        int nextBoundary = boundary + (w.z < 0.0 ? 1 : -1);
        if (nextBoundary < 0 || nextBoundary >= count)
        {
            s.wi     = w;
            s.weight = beta;

            s.flags      = historyFlags;
            s.lobe       = event.lobe;
            s.isDelta    = allDelta ? 1u : 0u;
            s.rrEtaScale = rrEtaScale;

            s.valid = any(beta > 0.0) &&
                      IsPathFinite3(s.wi) &&
                      IsPathFinite3(beta) &&
                      IsPathFinite(rrEtaScale);
            return s;
        }

        int slabIndex = min(boundary, nextBoundary);
        MaterialSlabData medium = Slabs[offset + slabIndex];
        float distance = medium.thickness / max(abs(w.z), EPSILON_MIN);
        // volume extinction
        float3 sigmaA = float3(medium.sigmaA_r, medium.sigmaA_g, medium.sigmaA_b);
        beta *= exp(-sigmaA * distance);
        if (!IsPathFinite3(beta) || !any(beta > 0.0))
            return s;

        boundary = nextBoundary;
        ++depth;

        const float rrThreshold = 0.05;
        if (depth >= rrStartDepth)
        {
            float3 rrBeta  = beta * rrEtaScale;
            float qSurvive = clamp(max3(rrBeta), rrThreshold, 1.0 - rrThreshold);
            if (NextFloat(rng) >= qSurvive)
                return s;

            beta /= qSurvive;
            qH   *= qSurvive;
        }
    }

    return s;
}

#if PT_VALIDATION
float3 SurfaceLobeMask(SurfaceMaterial material, float etaAbove, float etaBelow)
{
    float eta = max(etaBelow, 1.0e-4) / max(etaAbove, 1.0e-4);
    float3 wo = float3(0.0, 0.0, 1.0);
    LobeMixture ls = ResolveLobeMixture(material, eta, wo);
    return float3(
        ls.pmf.x > PT_LOBE_EPS ? 1.0 : 0.0,
        (ls.pmf.y > PT_LOBE_EPS || ls.pmf.z > PT_LOBE_EPS) ? 1.0 : 0.0,
        ls.pmf.w > PT_LOBE_EPS ? 1.0 : 0.0);
}

float3 SurfaceLobeWeight(SurfaceMaterial material, float3 wo, float etaAbove, float etaBelow)
{
    Layered::DielectricFrame frame = Layered::MakeDielectricFrame(wo, etaAbove, etaBelow);
    LobeMixture ls = ResolveLobeMixture(material, frame.eta, frame.wo);

    return float3(
        ls.pmf.x,
        ls.pmf.y + ls.pmf.z,
        ls.pmf.w);
}

float3 SampledLobeVector(PathBSDFSample sample)
{
    if (sample.attempted == 0u)
        return float3(0.0, 0.0, 0.0);
    if (sample.lobe == BxDF::LOBE_DIFFUSE)
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
    rng.seed    = seed;
    rng.counter = 0u;
    return rng;
}

bool LoadBoundaryData(
    SurfaceMaterial rootMaterial,
    float2 uv,
    float2 ddxUV,
    float2 ddyUV,
    float etaExterior,
    int boundary,
    uint layerOffset,
    out SurfaceMaterial material,
    out float etaAbove,
    out float etaBelow)
{
    material = rootMaterial;
    etaAbove = max(etaExterior, 1.0e-4);
    etaBelow = max(rootMaterial.ior, 1.0e-4);

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
    etaAbove = max(Materials[aboveSlab.materialID].ior, 1.0e-4);
    etaBelow = max(material.ior, 1.0e-4);
    return true;
}

float3 Evaluate(SurfaceMaterial rootMaterial, float2 uv, float2 ddxUV, float2 ddyUV, float3 wo, float3 wi, float etaExterior, uint querySeed)
{
    const float3 zero = float3(0.0, 0.0, 0.0);
    if (!IsPathFinite3(wo) || !IsPathFinite3(wi) ||
        abs(wo.z) <= EPSILON_MIN || abs(wi.z) <= EPSILON_MIN ||
        !IsPathFinite(etaExterior) || etaExterior <= 0.0)
    {
        return zero;
    }

    StructuredBuffer< MaterialSlabData > Slabs = GetResource(g_MaterialSlabs.index);

    int  count  = int(max(rootMaterial.layerCount, 1u));
    uint offset = rootMaterial.layerOffset;
    if (count > 1 && offset == INVALID_INDEX)
        return zero;

    int entryBoundary = wo.z > 0.0 ? 0 : count - 1;
    int exitBoundary  = wi.z > 0.0 ? 0 : count - 1;

    float3 result = zero;
    if (entryBoundary == exitBoundary)
    {
        SurfaceMaterial directMaterial;
        float etaAbove;
        float etaBelow;
        if (!LoadBoundaryData(rootMaterial, uv, ddxUV, ddyUV, etaExterior, entryBoundary, offset, directMaterial, etaAbove, etaBelow))
            return zero;

        PathContribution directLobes = LayerComposite::EvaluateBoundaryLobes(
            directMaterial,
            wo,
            wi,
            etaAbove,
            etaBelow);
        result += directLobes.diffuse + directLobes.specular + directLobes.transmission;
    }

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
                        etaExterior,
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
                Layered::LayerEvent reverseEvent = LayerComposite::SampleLayerEvent(
                    reverseMaterial,
                    reverseWo,
                    reverseEtaAbove,
                    reverseEtaBelow,
                    PT_TRANSPORT_IMPORTANCE,
                    reverseRng);
                if (!Layered::IsLayerEventValid(reverseEvent) || reverseEvent.isDelta != 0u)
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

                bool crossedBoundary = reverseW.z * reverseEvent.wi.z > 0.0;
                if (crossedBoundary != (reverseEvent.isTransmission != 0u))
                    return zero;

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
                        etaExterior,
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

                Layered::LayerEvent reverseEvent = LayerComposite::SampleLayerEvent(
                    reverseMaterial,
                    -reverseW,
                    reverseEtaAbove,
                    reverseEtaBelow,
                    PT_TRANSPORT_IMPORTANCE,
                    deltaRng);
                if (!Layered::IsLayerEventValid(reverseEvent) || reverseEvent.isDelta == 0u)
                    break;

                bHasReverseDelta = true;

                bool crossedBoundary = reverseW.z * reverseEvent.wi.z > 0.0;
                if (crossedBoundary != (reverseEvent.isTransmission != 0u))
                    return zero;

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
                etaExterior,
                forwardBoundary,
                offset,
                forwardMaterial,
                forwardEtaAbove,
                forwardEtaBelow))
        {
            return zero;
        }

        float3 forwardWo = -forwardW;
        Layered::LayerEvent forwardEvent = LayerComposite::SampleLayerEvent(
            forwardMaterial,
            forwardWo,
            forwardEtaAbove,
            forwardEtaBelow,
            PT_TRANSPORT_RADIANCE,
            forwardRng);
        if (!Layered::IsLayerEventValid(forwardEvent))
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

        bool crossedBoundary = forwardW.z * forwardEvent.wi.z > 0.0;
        if (crossedBoundary != (forwardEvent.isTransmission != 0u))
            return zero;

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
float MarginalPDF(SurfaceMaterial rootMaterial, float2 uv, float2 ddxUV, float2 ddyUV, float3 wo, float3 wi, float etaExterior, uint querySeed)
{
    if (!IsPathFinite3(wo) || !IsPathFinite3(wi) ||
        abs(wo.z) <= EPSILON_MIN || abs(wi.z) <= EPSILON_MIN ||
        !IsPathFinite(etaExterior) || etaExterior <= 0.0)
    {
        return 0.0;
    }

    int  count  = int(max(rootMaterial.layerCount, 1u));
    uint offset = rootMaterial.layerOffset;
    if (count > 1 && offset == INVALID_INDEX)
        return 0.0;

    int entryBoundary = wo.z > 0.0 ? 0 : count - 1;
    int exitBoundary  = wi.z > 0.0 ? 0 : count - 1;

    bool   hasContinuousProposal = false;
    int    probeBoundary         = entryBoundary;
    float3 probeW                = -wo;

    [loop]
    for (;;)
    {
        SurfaceMaterial probeMaterial;
        float probeEtaAbove;
        float probeEtaBelow;
        if (!LoadBoundaryData(
                rootMaterial,
                uv,
                ddxUV,
                ddyUV,
                etaExterior,
                probeBoundary,
                offset,
                probeMaterial,
                probeEtaAbove,
                probeEtaBelow))
        {
            return 0.0;
        }

        Layered::DielectricFrame frame = Layered::MakeDielectricFrame(-probeW, probeEtaAbove, probeEtaBelow);
        LayerComposite::LobeMixture mixture = LayerComposite::ResolveLobeMixture(probeMaterial, frame.eta, frame.wo);

        bool hasRoughReflection = probeMaterial.isSmooth == 0u &&
                                  mixture.pmf.y > 0.0;
        bool hasRoughTransmission = probeMaterial.isSmooth == 0u &&
                                    frame.eta != 1.0 &&
                                    mixture.pmf.w > 0.0;
        bool hasClearcoat = mixture.pmf.z > 0.0;
        hasContinuousProposal = mixture.pmf.x > 0.0 ||
                                hasRoughReflection ||
                                hasRoughTransmission ||
                                hasClearcoat;
        if (hasContinuousProposal)
            break;

        bool hasDeltaTransmission = mixture.pmf.w > 0.0 &&
                                    (probeMaterial.isSmooth != 0u || frame.eta == 1.0);
        if (!hasDeltaTransmission)
            break;

        float3 woLayer = BxDF::RotateXY(frame.wo, -GetAnisotropyRotation(probeMaterial));
        float3 wiLayer;
        float etaP;
        if (!BxDF::Transmission::Refract(
                woLayer,
                float3(0.0, 0.0, 1.0),
                frame.eta,
                wiLayer,
                etaP))
        {
            break;
        }

        float3 wiIncident = BxDF::RotateXY(wiLayer, GetAnisotropyRotation(probeMaterial));
        probeW = frame.bFlipped != 0u ? -wiIncident : wiIncident;

        int nextBoundary = probeBoundary + (probeW.z < 0.0 ? 1 : -1);
        if (nextBoundary < 0 || nextBoundary >= count)
            break;
        probeBoundary = nextBoundary;
    }

    // Atomic direction mass is not an ordinary sr^-1 density.
    if (!hasContinuousProposal)
        return 0.0;

    float result = 0.0;
    if (entryBoundary == exitBoundary)
    {
        SurfaceMaterial directMaterial;
        float etaAbove;
        float etaBelow;
        if (!LoadBoundaryData(rootMaterial, uv, ddxUV, ddyUV, etaExterior, entryBoundary, offset, directMaterial, etaAbove, etaBelow))
            return 0.0;

        result += LayerComposite::BoundaryMarginalPDF(
            directMaterial,
            wo,
            wi,
            etaAbove,
            etaBelow);
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
                        etaExterior,
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
                Layered::LayerEvent reverseEvent = LayerComposite::SampleLayerEvent(
                    reverseMaterial,
                    reverseWo,
                    reverseEtaAbove,
                    reverseEtaBelow,
                    PT_TRANSPORT_IMPORTANCE,
                    reverseRng);
                if (!Layered::IsLayerEventValid(reverseEvent) || reverseEvent.isDelta != 0u)
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

                bool crossedBoundary = reverseW.z * reverseEvent.wi.z > 0.0;
                if (crossedBoundary != (reverseEvent.isTransmission != 0u))
                    return 0.0;

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
                        etaExterior,
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
                Layered::LayerEvent reverseEvent = LayerComposite::SampleLayerEvent(
                    reverseMaterial,
                    reverseWo,
                    reverseEtaAbove,
                    reverseEtaBelow,
                    PT_TRANSPORT_IMPORTANCE,
                    deltaRng);
                if (!Layered::IsLayerEventValid(reverseEvent) || reverseEvent.isDelta == 0u)
                    break;

                bHasReverseDelta = true;

                bool crossedBoundary = reverseW.z * reverseEvent.wi.z > 0.0;
                if (crossedBoundary != (reverseEvent.isTransmission != 0u))
                    return 0.0;

                if (reverseEvent.isTransmission != 0u)
                {
                    // Current closures have symmetric delta masses. Refraction
                    // still changes the directional measure by this Jacobian.
                    float jacobian = BxDF::AbsCosTheta(reverseWo) /
                        max(sq(reverseEvent.eta) * BxDF::AbsCosTheta(reverseEvent.wi), EPSILON_MIN);
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
                etaExterior,
                forwardBoundary,
                offset,
                forwardMaterial,
                forwardEtaAbove,
                forwardEtaBelow))
        {
            return 0.0;
        }

        float3 forwardWo = -forwardW;
        Layered::LayerEvent forwardEvent = LayerComposite::SampleLayerEvent(
            forwardMaterial,
            forwardWo,
            forwardEtaAbove,
            forwardEtaBelow,
            PT_TRANSPORT_RADIANCE,
            forwardRng);
        if (!Layered::IsLayerEventValid(forwardEvent))
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

        bool crossedBoundary = forwardW.z * forwardEvent.wi.z > 0.0;
        if (crossedBoundary != (forwardEvent.isTransmission != 0u))
            return 0.0;

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
        return 0.0;

    return result;
}
} // namespace DirectionalComposite

} // namespace BxDF

#endif // _HLSL_PATHCOMPOSITE_HEADER
