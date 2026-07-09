#ifndef _HLSL_PATHCOMPOSITE_HEADER
#define _HLSL_PATHCOMPOSITE_HEADER

#include "PathUtils.hlsli"
#include "Sampling.hlsli"

namespace BxDF
{
namespace Composite
{

static const uint LOBE_SLOT_DIFFUSE      = 0u;
static const uint LOBE_SLOT_SPECULAR     = 1u;
static const uint LOBE_SLOT_CLEARCOAT    = 2u;
static const uint LOBE_SLOT_TRANSMISSION = 3u;
static const uint LOBE_SLOT_COUNT        = 4u;

bool IsSmoothConductor(SurfaceMaterial material)
{
    return !IsPrincipledMaterial(material) &&
           !HasTransmissionLobe(material) &&
           !HasClearcoatLobe(material) &&
           !HasSheenLobe(material) &&
           saturate(material.metallic) > 1.0 - PT_LOBE_EPS &&
           material.roughness <= PT_SMOOTH_ROUGHNESS;
}

struct LobeMixture
{
    // diffuse, specular reflection, clearcoat, transmission
    float4 pmf;    // sampling probability
    float4 weight; // BSDF mixture scale
};

float4 NormalizeLobePMF(float4 weights)
{
    float weightSum = weights.x + weights.y + weights.z + weights.w;
    if (weightSum <= EPSILON_MIN)
        return float4(1.0, 0.0, 0.0, 0.0);
    return weights / weightSum;
}

float MaxComponent(float3 value)
{
    return max(max(value.x, value.y), value.z);
}

float NonPrincipledOpaqueWeight(SurfaceMaterial material)
{
    if (IsPrincipledMaterial(material) || HasTransmissionLobe(material))
        return 0.0;

    return 1.0 - saturate(material.metallic);
}

float NonPrincipledConductorWeight(SurfaceMaterial material)
{
    if (IsPrincipledMaterial(material))
        return 0.0;

    float transmission = saturate(material.transmission);
    return (1.0 - transmission) * saturate(material.metallic);
}

float DielectricSpecularF0(SurfaceMaterial material)
{
    if (!HasDielectricSpecularLobe(material))
        return 0.0;

    float eta = max(material.ior, 1.0);
    float f0  = (eta - 1.0) / (eta + 1.0);
    f0 *= f0;
    return saturate(material.specularStrength) * MaxComponent(saturate(material.specularColor)) * f0;
}

LobeMixture ResolveLobeMixture(SurfaceMaterial material)
{
    LobeMixture ls;
    ls.pmf    = float4(0.0, 0.0, 0.0, 0.0);
    ls.weight = float4(0.0, 0.0, 0.0, 0.0);

    float metallic     = saturate(material.metallic);
    float transmission = saturate(material.transmission);
    float opaque       = 1.0 - transmission;

    float opaqueDiffuseWeight   = opaque * (1.0 - metallic);
    float diffuseSamplingWeight = opaqueDiffuseWeight;
    float sheenWeight           = SheenSamplingWeight(material);
    float diffuseWeight         = max(diffuseSamplingWeight, sheenWeight);
    float specularWeight 
            = IsPrincipledMaterial(material)
            ? 1.0
            : NonPrincipledConductorWeight(material) + NonPrincipledOpaqueWeight(material) * DielectricSpecularF0(material);
    float clearcoatWeight       = IsPrincipledMaterial(material) ? saturate(material.clearcoat) * 0.25 : saturate(material.clearcoat);
    float transmissionWeight    = IsPrincipledMaterial(material) ? 0.0 : transmission;

    if (IsSmoothConductor(material))
    {
        ls.weight.y = 1.0;
        ls.pmf.y    = 1.0;
        return ls;
    }

    ls.weight = IsPrincipledMaterial(material)
        ? float4(1.0, 1.0, 1.0, 0.0)
        : float4(diffuseSamplingWeight, specularWeight, clearcoatWeight, transmissionWeight);

    ls.pmf = NormalizeLobePMF(float4(diffuseWeight, specularWeight, clearcoatWeight, transmissionWeight));
    return ls;
}
struct DielectricFrame
{
    uint   bFlipped;
    float  eta;
    float3 wo;
};

DielectricFrame MakeDielectricFrame(SurfaceMaterial material, float3 wo)
{
    DielectricFrame frame;
    frame.bFlipped = wo.z < 0.0 ? 1u : 0u;
            
    float eta = max(material.ior, 1.0001);
    frame.eta = frame.bFlipped != 0u ? rcp(max(eta, 1.0e-4)) : eta;
    frame.wo  = frame.bFlipped != 0u ? -wo : wo;
            
    return frame;
}

float DielectricViewFresnel(SurfaceMaterial material, float3 wo)
{
    DielectricFrame frame = MakeDielectricFrame(material, wo);
    return saturate(BxDF::Fresnel::Dielectric(BxDF::CosTheta(frame.wo), 1.0, frame.eta));
}

float3 EvaluateDielectricReflection(SurfaceMaterial material, float3 wo, float3 wi)
{
    DielectricFrame frame = MakeDielectricFrame(material, wo);
    wo = frame.wo;
    wi = frame.bFlipped != 0u ? -wi : wi;

    float2 alpha = GetAlpha2(material);
    return BxDF::Dielectric::EvaluateBRDF(wo, wi, alpha.x, alpha.y, frame.eta);
}
float3 EvaluateDielectricTransmission(SurfaceMaterial material, float3 wo, float3 wi)
{
    DielectricFrame frame = MakeDielectricFrame(material, wo);
    wo = frame.wo;
    wi = frame.bFlipped != 0u ? -wi : wi;

    float2 alpha = GetAlpha2(material);
    return BxDF::Dielectric::EvaluateBTDF(wo, wi, alpha.x, alpha.y, frame.eta);
}

float DielectricReflectionPDF(SurfaceMaterial material, float3 wo, float3 wi)
{
    DielectricFrame frame = MakeDielectricFrame(material, wo);
    wo = frame.wo;
    wi = frame.bFlipped != 0u ? -wi : wi;

    if (!BxDF::SameHemisphere(wo, wi))
        return 0.0;

    float3 wh = wo + wi;
    if (dot(wh, wh) == 0.0)
        return 0.0;
    wh = normalize(wh);
    if (wh.z < 0.0)
        wh = -wh;

    float2 alpha = GetAlpha2(material);
    float R     = BxDF::Fresnel::Dielectric(dot(wo, wh), 1.0, frame.eta);
    float T     = 1.0 - R;
    float denom = max(R + T, EPSILON_MIN);
    return BxDF::GGX::EvaluatePDF(wo, wi, alpha.x, alpha.y) * (R / denom);
}

float DielectricTransmissionPDF(SurfaceMaterial material, float3 wo, float3 wi)
{
    DielectricFrame frame = MakeDielectricFrame(material, wo);
    wo = frame.wo;
    wi = frame.bFlipped != 0u ? -wi : wi;

    float2 alpha = GetAlpha2(material);
    float etaP;
    float3 wh;
    if (!BxDF::Dielectric::IsTransmittable(wo, wi, frame.eta, wh, etaP))
        return 0.0;

    float R     = BxDF::Fresnel::Dielectric(dot(wo, wh), 1.0, frame.eta);
    float T     = 1.0 - R;
    float denom = max(R + T, EPSILON_MIN);
    return BxDF::Dielectric::EvaluatePDF(wo, wi, alpha.x, alpha.y, frame.eta) * (T / denom);
}
        

float3 EvaluateDiffuseBRDF(SurfaceMaterial material, float3 wo, float3 wi)
{
    if (!BxDF::SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);

    return IsPrincipledMaterial(material)
        ? BxDF::Diffuse::EvaluateBRDF(material.albedo, material.roughness, wo, wi)
        : BxDF::Diffuse::Lambert(material.albedo);
}

float3 EvaluateSheenBRDF(SurfaceMaterial material, float3 wo, float3 wi)
{
    if (!HasSheenLobe(material) || !BxDF::SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);

    if (!IsPrincipledMaterial(material))
        return BxDF::Sheen::EvaluateBRDF(material.sheenColor, material.sheenRoughness, wo, wi);

    float3 h = wo + wi;
    float  hLenSq = dot(h, h);
    if (hLenSq <= 0.0)
        return float3(0.0, 0.0, 0.0);

    h = h * rsqrt(hLenSq);
    float sheenWeight = pow(saturate(1.0 - dot(wi, h)), 5.0);
    return material.sheenColor * sheenWeight;
}

float3 EvaluateSpecularBRDF(SurfaceMaterial material, float3 wo, float3 wi)
{
    if (!BxDF::SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);

    if (!IsPrincipledMaterial(material))
    {
        if (IsSmoothConductor(material))
            return float3(0.0, 0.0, 0.0);

        float2 alpha = GetAlpha2(material);
        float3 f = float3(0.0, 0.0, 0.0);

        float conductorWeight = NonPrincipledConductorWeight(material);
        if (conductorWeight > PT_LOBE_EPS)
            f += conductorWeight * BxDF::GGX::EvaluateBRDF(wo, wi, material.specularColor, alpha.x, alpha.y);

        float opaqueDielectricWeight = NonPrincipledOpaqueWeight(material);
        if (opaqueDielectricWeight > PT_LOBE_EPS && HasDielectricSpecularLobe(material))
        {
            float3 specularScale = saturate(material.specularColor) * saturate(material.specularStrength);
            f += opaqueDielectricWeight * specularScale * EvaluateDielectricReflection(material, wo, wi);
        }

        return f;
    }

    float3 h = wo + wi;
    float  hLenSq = dot(h, h);
    if (hLenSq <= 0.0)
        return float3(0.0, 0.0, 0.0);
    h = h * rsqrt(hLenSq);

    float WoH = saturate(dot(wo, h));
    if (WoH <= 0.0)
        return float3(0.0, 0.0, 0.0);

    float2 alpha = GetAlpha2(material);
    
    float  D = BxDF::GGX::D(h, alpha.x, alpha.y);
    float  G = BxDF::GGX::G2(wo, wi, alpha.x, alpha.y);
    
    float eta = max(material.ior, 1.0e-4);
    float f0Eta = (eta - 1.0) / (eta + 1.0);
    f0Eta *= f0Eta;

    float  dielectricF = BxDF::Fresnel::Dielectric(WoH, 1.0, eta);
    float3 dielectricTint = (f0Eta > 1.0e-6) ? max(material.specularColor / f0Eta, float3(0.0, 0.0, 0.0)) : float3(1.0, 1.0, 1.0);
    float3 dielectricFresnel = (1.0 - saturate(material.metallic)) * dielectricF * dielectricTint;
    float3 metallicFresnel = saturate(material.metallic) * BxDF::Fresnel::Schlick(material.albedo, WoH);
    float3 F = (dielectricFresnel + metallicFresnel) * material.specularStrength;
    
    float  denom = 4.0 * BxDF::AbsCosTheta(wo) * BxDF::AbsCosTheta(wi);
    return (denom > 0.0) ? (F * D * G / denom) : float3(0.0, 0.0, 0.0);
}

float3 EvaluateClearcoatBRDF(SurfaceMaterial material, float3 wo, float3 wi)
{
    if (!HasClearcoatLobe(material) || !BxDF::SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);

    float3 clearcoatBRDF = BxDF::Clearcoat::EvaluateBRDF(wo, wi, max(material.clearcoatRoughness, 1.0e-3));
    if (!IsPrincipledMaterial(material))
        return saturate(material.clearcoat) * clearcoatBRDF;

    float noV = BxDF::AbsCosTheta(wo);
    float noL = BxDF::AbsCosTheta(wi);
    
    // disney-principled clearcoat: BxDF keeps the physical denominator; this adapter matches Disney/Mitsuba.
    float disneyClearcoatScale = 4.0 * noV * noL;
    return (material.clearcoat * 0.25) * disneyClearcoatScale * clearcoatBRDF;
}
        

PathContribution EvaluateLobeSlot(SurfaceMaterial material, float3 wo, float3 wi, uint slot, LobeMixture ls)
{
    PathContribution lobes = ZeroPathContribution();
    if (slot == LOBE_SLOT_DIFFUSE)
    {
        lobes.diffuse = EvaluateDiffuseBRDF(material, wo, wi);
        if (!IsPrincipledMaterial(material))
            lobes.diffuse *= ls.weight.x;
        lobes.diffuse += EvaluateSheenBRDF(material, wo, wi);
        return lobes;
    }

    if (slot == LOBE_SLOT_SPECULAR)
    {
        lobes.specular = EvaluateSpecularBRDF(material, wo, wi);
        return lobes;
    }

    if (slot == LOBE_SLOT_CLEARCOAT)
    {
        lobes.specular = EvaluateClearcoatBRDF(material, wo, wi);
        return lobes;
    }

    if (slot == LOBE_SLOT_TRANSMISSION)
    {
        float transmissionWeight = ls.weight.w;
        // A transmissive dielectric component still carries its Fresnel reflection.
        if (BxDF::SameHemisphere(wo, wi))
            lobes.specular = transmissionWeight * EvaluateDielectricReflection(material, wo, wi);
        else
            lobes.transmission = transmissionWeight * EvaluateDielectricTransmission(material, wo, wi);
    }
            
    return lobes;
}

float PDFLobeSlot(SurfaceMaterial material, float3 wo, float3 wi, uint slot)
{
    if (slot == LOBE_SLOT_DIFFUSE)
        return BxDF::Diffuse::EvaluatePDF(wo, wi);

    if (slot == LOBE_SLOT_SPECULAR)
    {
        if (IsSmoothConductor(material))
            return 0.0;

        float2 alpha = GetAlpha2(material);
        return BxDF::GGX::EvaluatePDF(wo, wi, alpha.x, alpha.y);
    }

    if (slot == LOBE_SLOT_CLEARCOAT)
        return BxDF::Clearcoat::EvaluatePDF(wo, wi, max(material.clearcoatRoughness, 1.0e-3));

    if (slot == LOBE_SLOT_TRANSMISSION)
    {
        float dielectricPdf = BxDF::SameHemisphere(wo, wi)
            ? DielectricReflectionPDF(material, wo, wi)
            : DielectricTransmissionPDF(material, wo, wi);
        return dielectricPdf;
    }

    return 0.0;
}

PathContribution AddPathContribution(PathContribution a, PathContribution b)
{
    a.diffuse      += b.diffuse;
    a.specular     += b.specular;
    a.transmission += b.transmission;
    return a;
}

PathContribution EvaluateLobes(SurfaceMaterial material, float3 wo, float3 wi)
{
    LobeMixture ls = ResolveLobeMixture(material);
            
    PathContribution lobes = ZeroPathContribution();
    lobes = AddPathContribution(lobes, EvaluateLobeSlot(material, wo, wi, LOBE_SLOT_DIFFUSE, ls));
    lobes = AddPathContribution(lobes, EvaluateLobeSlot(material, wo, wi, LOBE_SLOT_SPECULAR, ls));
    lobes = AddPathContribution(lobes, EvaluateLobeSlot(material, wo, wi, LOBE_SLOT_CLEARCOAT, ls));
    lobes = AddPathContribution(lobes, EvaluateLobeSlot(material, wo, wi, LOBE_SLOT_TRANSMISSION, ls));
    return lobes;
}

float3 Evaluate(SurfaceMaterial material, float3 wo, float3 wi, out uint flags)
{
    flags = 0u;
    PathContribution lobes = EvaluateLobes(material, wo, wi);
            
    if (any(lobes.diffuse > 0.0))
        flags |= PT_BSDF_FLAG_DIFFUSE;
    if (any(lobes.specular > 0.0))
        flags |= PT_BSDF_FLAG_GLOSSY;
    if (any(lobes.transmission > 0.0))
        flags |= PT_BSDF_FLAG_TRANSMISSION;
            
    return lobes.diffuse + lobes.specular + lobes.transmission;
}

float PDF(SurfaceMaterial material, float3 wo, float3 wi)
{
    LobeMixture ls = ResolveLobeMixture(material);
    return PDFLobeSlot(material, wo, wi, LOBE_SLOT_DIFFUSE) * ls.pmf.x +
           PDFLobeSlot(material, wo, wi, LOBE_SLOT_SPECULAR) * ls.pmf.y +
           PDFLobeSlot(material, wo, wi, LOBE_SLOT_CLEARCOAT) * ls.pmf.z +
           PDFLobeSlot(material, wo, wi, LOBE_SLOT_TRANSMISSION) * ls.pmf.w;
}
        
PathBSDFSample InitializeSample()
{
    PathBSDFSample sample;
    sample.wi        = float3(0.0, 0.0, 0.0);
    sample.pdf       = 0.0;
    sample.f         = float3(0.0, 0.0, 0.0);
    sample.flags     = 0u;
    sample.weight    = float3(0.0, 0.0, 0.0);
    sample.valid     = 0u;
    sample.lobe      = BxDF::LOBE_DIFFUSE;
    sample.isDelta   = 0u;
    sample.attempted = 0u;
    return sample;
}


PathBSDFSample FinalizeLobeSample(SurfaceMaterial material, float3 wo, PathBSDFSample sample)
{
    sample.pdf = PDF(material, wo, sample.wi);
    if (sample.pdf <= 0.0)
        return sample;

    uint evaluatedFlags;
    sample.f      = Evaluate(material, wo, sample.wi, evaluatedFlags);
    sample.flags  = evaluatedFlags;
    sample.weight = sample.f * BxDF::AbsCosTheta(sample.wi) / sample.pdf;
    sample.valid  = any(sample.weight > 0.0) ? 1u : 0u;
    return sample;
}

float LobeSlotProbability(LobeMixture ls, uint slot)
{
    if (slot == LOBE_SLOT_DIFFUSE)
        return ls.pmf.x;
    if (slot == LOBE_SLOT_SPECULAR)
        return ls.pmf.y;
    if (slot == LOBE_SLOT_CLEARCOAT)
        return ls.pmf.z;
    return ls.pmf.w;
}

uint ChooseLobeSlot(LobeMixture ls, float uc, out float lobeUc)
{
    float remainingUc = saturate(uc);

    float cumulative = ls.pmf.x;
    if (ls.pmf.x > PT_LOBE_EPS && remainingUc < cumulative)
    {
        lobeUc = saturate(remainingUc / max(ls.pmf.x, EPSILON_MIN));
        return LOBE_SLOT_DIFFUSE;
    }

    float previous = cumulative;
    cumulative += ls.pmf.y;
    if (ls.pmf.y > PT_LOBE_EPS && remainingUc < cumulative)
    {
        lobeUc = saturate((remainingUc - previous) / max(ls.pmf.y, EPSILON_MIN));
        return LOBE_SLOT_SPECULAR;
    }

    previous = cumulative;
    cumulative += ls.pmf.z;
    if (ls.pmf.z > PT_LOBE_EPS && remainingUc < cumulative)
    {
        lobeUc = saturate((remainingUc - previous) / max(ls.pmf.z, EPSILON_MIN));
        return LOBE_SLOT_CLEARCOAT;
    }

    previous = cumulative;
    if (ls.pmf.w > PT_LOBE_EPS)
    {
        lobeUc = saturate((remainingUc - previous) / max(ls.pmf.w, EPSILON_MIN));
        return LOBE_SLOT_TRANSMISSION;
    }

    lobeUc = 0.0;
    return LOBE_SLOT_DIFFUSE;
}

PathBSDFSample SampleDiffuseLobe(SurfaceMaterial material, float3 wo, float2 u)
{
    PathBSDFSample sample = InitializeSample();
    if (wo.z <= 0.0)
        return sample;

    sample.wi        = BxDF::Diffuse::SampleRay(wo, u);
    sample.lobe      = BxDF::LOBE_DIFFUSE;
    sample.flags     = PT_BSDF_FLAG_DIFFUSE;
    sample.isDelta   = 0u;
    sample.attempted = 1u;
    if (sample.wi.z <= 0.0)
        return sample;

    return FinalizeLobeSample(material, wo, sample);
}

PathBSDFSample SampleSpecularLobe(SurfaceMaterial material, float3 wo, float2 u)
{
    PathBSDFSample sample = InitializeSample();
    if (wo.z <= 0.0)
        return sample;
            
    if (IsSmoothConductor(material))
    {
        sample.wi        = float3(-wo.x, -wo.y, wo.z);
        sample.pdf       = 0.0;
        sample.weight    = BxDF::Fresnel::Schlick(material.specularColor, saturate(BxDF::AbsCosTheta(wo)));
        sample.flags     = PT_BSDF_FLAG_GLOSSY;
        sample.valid     = any(sample.weight > 0.0) ? 1u : 0u;
        sample.lobe      = BxDF::LOBE_SPECULAR;
        sample.isDelta   = 1u;
        sample.attempted = 1u;
        return sample;
    }

    float2 alpha = GetAlpha2(material);
    
    float3 wh = BxDF::GGX::SampleRay(wo, alpha.x, alpha.y, u);
    sample.wi        = reflect(-wo, wh);
    sample.lobe      = BxDF::LOBE_SPECULAR;
    sample.flags     = PT_BSDF_FLAG_GLOSSY;
    sample.isDelta   = 0u;
    sample.attempted = 1u;
    if (sample.wi.z <= 0.0)
        return sample;

    return FinalizeLobeSample(material, wo, sample);
}

PathBSDFSample SampleClearcoatLobe(SurfaceMaterial material, float3 wo, float2 u)
{
    PathBSDFSample sample = InitializeSample();
    if (wo.z <= 0.0)
        return sample;

    sample.wi        = BxDF::Clearcoat::SampleRay(wo, max(material.clearcoatRoughness, 1.0e-3), u);
    sample.lobe      = BxDF::LOBE_CLEARCOAT;
    sample.flags     = PT_BSDF_FLAG_GLOSSY;
    sample.isDelta   = 0u;
    sample.attempted = 1u;
    if (sample.wi.z <= 0.0)
        return sample;

    return FinalizeLobeSample(material, wo, sample);
}

PathBSDFSample SampleTransmissionLobe(SurfaceMaterial material, float3 wo, float lobeUc, float2 u, LobeMixture ls)
{
    PathBSDFSample sample = InitializeSample();

    DielectricFrame frame = MakeDielectricFrame(material, wo);
    float2 alpha = GetAlpha2(material);
            
    BxDF::BSDFSample bs = BxDF::Dielectric::SampleRay(frame.wo, alpha.x, alpha.y, frame.eta, lobeUc, u);
    if (bs.pdf <= 0.0 && bs.isDelta == 0u)
        return sample;

    sample.wi        = frame.bFlipped != 0u ? -bs.wi : bs.wi;
    sample.lobe      = bs.lobe;
    sample.flags     = (bs.lobe == BxDF::LOBE_TRANSMISSION) ? PT_BSDF_FLAG_TRANSMISSION : PT_BSDF_FLAG_GLOSSY;
    sample.isDelta   = bs.isDelta;
    sample.attempted = 1u;

    if (bs.isDelta != 0u)
    {
        sample.pdf    = (ls.pmf.w >= 1.0 - PT_LOBE_EPS) ? bs.pdf : 0.0;
        sample.weight = bs.weight;
        sample.f      = Evaluate(material, wo, sample.wi, sample.flags);
        sample.valid  = any(sample.weight > 0.0) ? 1u : 0u;
        return sample;
    }

    if (ls.weight.w >= 1.0 - PT_LOBE_EPS)
    {
        sample.pdf    = bs.pdf;
        sample.weight = bs.weight;
        sample.f      = Evaluate(material, wo, sample.wi, sample.flags);
        sample.valid  = any(sample.weight > 0.0) ? 1u : 0u;
        return sample;
    }

    return FinalizeLobeSample(material, wo, sample);
}
PathBSDFSample SampleLobe(SurfaceMaterial material, float3 wo, uint slot, float lobeUc, float2 u, LobeMixture ls)
{
    if (LobeSlotProbability(ls, slot) <= PT_LOBE_EPS)
        return InitializeSample();

    if (slot == LOBE_SLOT_DIFFUSE)
        return SampleDiffuseLobe(material, wo, u);
    if (slot == LOBE_SLOT_SPECULAR)
        return SampleSpecularLobe(material, wo, u);
    if (slot == LOBE_SLOT_CLEARCOAT)
        return SampleClearcoatLobe(material, wo, u);
    if (slot == LOBE_SLOT_TRANSMISSION)
        return SampleTransmissionLobe(material, wo, lobeUc, u, ls);
            
    return InitializeSample();
}

PathBSDFSample SampleRay(SurfaceMaterial material, float3 wo, inout RngState rng)
{
    LobeMixture ls = ResolveLobeMixture(material);

    float lobeUc;
    uint  slot = ChooseLobeSlot(ls, NextFloat(rng), lobeUc);
            
    return SampleLobe(material, wo, slot, lobeUc, NextFloat2(rng), ls);
}

#if PT_VALIDATION
float3 SurfaceLobeMask(SurfaceMaterial material)
{
    LobeMixture ls = ResolveLobeMixture(material);
    return float3(
        ls.pmf.x > PT_LOBE_EPS ? 1.0 : 0.0,
        (ls.pmf.y > PT_LOBE_EPS || ls.pmf.z > PT_LOBE_EPS || ls.pmf.w > PT_LOBE_EPS) ? 1.0 : 0.0,
        ls.pmf.w > PT_LOBE_EPS ? 1.0 : 0.0);
}

float3 SurfaceLobeWeight(SurfaceMaterial material, float3 wo)
{
    LobeMixture ls = ResolveLobeMixture(material);

    float transmissionFresnel = (ls.pmf.w > PT_LOBE_EPS) ? DielectricViewFresnel(material, wo) : 0.0;
    return float3(
        ls.pmf.x,
        ls.pmf.y + ls.pmf.z + ls.pmf.w * transmissionFresnel,
        ls.pmf.w * (1.0 - transmissionFresnel));
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

} // namespace Composite

} // namespace BxDF

#endif // _HLSL_PATHCOMPOSITE_HEADER
