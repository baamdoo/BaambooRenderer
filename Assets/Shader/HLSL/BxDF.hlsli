#ifndef _HLSL_BXDF_HEADER
#define _HLSL_BXDF_HEADER

#include "Common.hlsli"
#include "HelperFunctions.hlsli"

static const uint PT_TRANSPORT_RADIANCE   = 0u;
static const uint PT_TRANSPORT_IMPORTANCE = 1u;

namespace BxDF
{

// ── Common types ─────────────────────────────────────────────────────────
// Conventions : Lobe is in LOCAL FRAME (N = +z, T = +x, B = +y)
    
// Tangent-space basis at a shading point. T x B == N (right-handed local).
struct Frame
{
    float3 T;
    float3 B;
    float3 N;
};

struct ScatteringModelSample
{
    float3 wi;
    float3 weight;
    float  pdf;     // solid-angle PDF for continuous samples; PMF for delta samples
    uint   lobe;
    uint   isDelta;
};

// Lobe IDs
static const uint LOBE_DIFFUSE      = 0u;
static const uint LOBE_SPECULAR     = 1u;
static const uint LOBE_CLEARCOAT    = 2u;
static const uint LOBE_TRANSMISSION = 3u;
static const uint LOBE_SHEEN        = 4u;
static const uint LOBE_SUBSURFACE   = 5u;

// ── Local-frame helpers (w.z == cos θ) ────────────
float CosTheta    (float3 w) { return w.z; }
float Cos2Theta   (float3 w) { return w.z * w.z; }
float AbsCosTheta (float3 w) { return abs(w.z); }

bool SameHemisphere(float3 wo, float3 wi) { return wo.z * wi.z > 0.0; }

bool SameDirection(float3 a, float3 b) { float3 d = a - b; return dot(d, d) <= 1.0e-6; }
float3 RotateXY(float3 w, float rotation)
{
    float s, c;
    sincos(rotation, s, c);

    // rotate xy-plane against z-axis
    return float3(
        w.x * c - w.y * s,
        w.x * s + w.y * c,
        w.z
    );
}

// ── World ↔ Local conversion helpers ─────────────────────────────────────────────
float3 ToLocal(Frame f, float3 vW)
{
    return float3(dot(vW, f.T), dot(vW, f.B), dot(vW, f.N));
}

float3 ToWorld(Frame f, float3 vL)
{
    return f.T * vL.x + f.B * vL.y + f.N * vL.z;
}

// ── Helpers ─────────────────────────────
float GetTransmissionScale(float resolvedEtaTOverI, uint mode)
{
    return mode == PT_TRANSPORT_RADIANCE ? rcp(resolvedEtaTOverI * resolvedEtaTOverI) : 1.0;
}

// Reference: https://seblagarde.wordpress.com/2013/04/29/memo-on-fresnel-equations/
namespace Fresnel
{

float3 Schlick(float3 F0, float cosTheta)
{
    float a  = saturate(1.0 - cosTheta);
    float a2 = a * a;
    float a5 = a2 * a2 * a;
    return F0 + (1.0 - F0) * a5;
}

// Exact unpolarized Fresnel reflectance for a dielectric/dielectric interface.
float Dielectric(float cosThetaI, float ior1, float ior2)
{
    float etaTOverI = ior2 / ior1;
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);

    // Back face: ray exiting denser side. Swap so the math runs as "entering".
    if (cosThetaI < 0.0)
    {
        etaTOverI = 1.0 / etaTOverI;
        cosThetaI = -cosThetaI;
    }

    // Snell: sinθ_t = (η_i / η_t) · sinθ_i.
    float sinThetaI = sqrt(max(0.0, 1.0 - cosThetaI * cosThetaI));
    float sinThetaT = sinThetaI * (1.0 / etaTOverI);

    // TIR: full reflection (no transmittance).
    if (sinThetaT >= 1.0)
        return 1.0;
            
    float cosThetaT = safeSqrt(1.0 - sinThetaT * sinThetaT);
            
    float Rparl = (etaTOverI * cosThetaI - cosThetaT) / (etaTOverI * cosThetaI + cosThetaT);
    float Rperp = (cosThetaI - etaTOverI * cosThetaT) / (cosThetaI + etaTOverI * cosThetaT);

    return (Rparl * Rparl + Rperp * Rperp) / 2.0;
}

} // namespace Fresnel
    
namespace GGX
{

bool IsSmooth(float aT, float aB)
{
    return max(aT, aB) < 1.0e-3;
}
        
float Lambda(float3 w, float aT, float aB)
{
    float a2Inv = (sq(aT * w.x) + sq(aB * w.y));
    return (sqrt(1.0 + a2Inv / sq(w.z)) - 1.0) * 0.5;
}
        
float D(float3 h, float aT, float aB)
{
    float d = sq(h.x / aT) + sq(h.y / aB) + sq(h.z);
    return 1.0 / (PI * aT * aB * sq(d));
}

float G1(float3 w, float aT, float aB)
{
    return 1.0 / (1.0 + Lambda(w, aT, aB));
}
        
float G2(float3 wo, float3 wi, float aT, float aB)
{
    return 1.0 / (1.0 + Lambda(wo, aT, aB) + Lambda(wi, aT, aB));
}

// Reference: https://jcgt.org/published/0007/04/01/paper.pdf
float3 SampleVisibleNormal(float3 wo, float aT, float aB, float2 u)
{
    float3 Vh = normalize(float3(aT * wo.x, aB * wo.y, wo.z));
    
    float  len2 = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = len2 > 0.0 ? 
            float3(-Vh.y, Vh.x, 0.0) * (1.0 / safeSqrt(len2)) : float3(1.0, 0.0, 0.0); // cross-product
    float3 T2 = cross(Vh, T1);
            
    float r   = safeSqrt(u.x);
    float phi = 2.0 * PI * u.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    
    float s = 0.5 * (1.0 + Vh.z); // shrink-scale
    t2 = (1.0 - s) * safeSqrt(1.0 - t1 * t1) + s * t2;
            
    float3 Nh = t1 * T1 + t2 * T2 + safeSqrt(1.0 - t1 * t1 - t2 * t2) * Vh; // eq.hemisphere
    float3 Ne = normalize(float3(aT * Nh.x, aB * Nh.y, max(0.0, Nh.z)));
    return Ne;
}

} // namespace GGX


// ── Lobes ─────────────────────────────

namespace Lobe
{
    
// Reference: https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf    
namespace Diffuse
{

float3 Lambert(float3 albedo)
{
    return albedo * (1.0 / PI);
}

float EvaluatePDF(float3 wo, float3 wi)
{
    if (!SameHemisphere(wo, wi))
        return 0.0;
    return AbsCosTheta(wi) * (1.0 / PI);
}

// Disney "Burley" diffuse — Burley 2012 §5.3.
float3 EvaluateBRDF(float3 albedo, float roughness, float3 wo, float3 wi)
{
    float i  = 1.0 - CosTheta(wi);
    float i2 = i * i;
    float i5 = i2 * i2 * i;
            
    float o  = 1.0 - CosTheta(wo);
    float o2 = o * o;
    float o5 = o2 * o2 * o;
            
    float3 H    = normalize(wo + wi);
    float  LoH  = saturate(dot(wi, H));
    float  FD90 = 0.5 + 2.0 * roughness * LoH * LoH;
            
    float3 f = (albedo / PI) * (1.0 + (FD90 - 1.0) * i5) * (1.0 + (FD90 - 1.0) * o5);
    return f;
}

float3 SampleRay(float3 wo, float2 u)
{
    float  r   = safeSqrt(u.x);
    float  phi = 2.0 * PI * u.y;
    float3 wi  = float3(r * cos(phi), r * sin(phi), safeSqrt(1.0 - u.x));
    return (wo.z < 0.0) ? float3(wi.x, wi.y, -wi.z) : wi;
}

} // namespace Diffuse    
    
// Reference: https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
namespace Sheen
{

float D_Charlie(float roughness, float NoH)
{
    float a    = max(roughness * roughness, 0.0078125);   // min α² guard (~0.0078 = 1/128)
    float invA = 1.0 / a;
    float sin2 = max(1.0 - NoH * NoH, 0.0078125);
    return (2.0 + invA) * pow(sin2, invA * 0.5) / (2.0 * PI);
}

float V_Ashikhmin(float NoL, float NoV)
{
    return 1.0 / (4.0 * (NoL + NoV - NoL * NoV) + 1e-7);
}

float3 EvaluateBRDF(float3 sheenColor, float sheenRoughness, float3 wo, float3 wi)
{
    if (!SameHemisphere(wo, wi))
        return 0.0;
            
    float3 H = normalize(wo + wi);
            
    float NoH = saturate(H.z);
    float NoL = saturate(wi.z);
    float NoV = saturate(wo.z);
            
    float D = D_Charlie(sheenRoughness, NoH);
    float V = V_Ashikhmin(NoL, NoV);
    return sheenColor * D * V;
}

} // namespace Sheen

namespace Reflection
{
    
float EvaluateMicrofacetPDF(float3 wo, float3 wi, float aT, float aB)
{
    if (GGX::IsSmooth(aT, aB))
        return 0.0;

    if (!SameHemisphere(wo, wi))
        return 0.0;
    
    float3 wh = normalize(wi + wo);
    if (dot(wo, wh) <= 0)
        return 0;
            
    float D = GGX::D(wh, aT, aB);
    float G = GGX::G1(wo, aT, aB);
    
    return D * G / (4.0 * AbsCosTheta(wo));
}

float3 EvaluateMicrofacetBRDF(float3 wo, float3 wi, float3 F, float aT, float aB)
{
    if (GGX::IsSmooth(aT, aB))
        return 0.0;
            
    if (!SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);
    
    float3 wh = normalize(wi + wo);
    if (CosTheta(wh) < 0.0)
        wh = -wh;
    
    float denominator = 4.0 * AbsCosTheta(wo) * AbsCosTheta(wi);
    if (denominator <= 1.0e-6)
        return 0.0;
            
    float  D = GGX::D(wh, aT, aB);
    float  G = GGX::G2(wo, wi, aT, aB);
    return F * D * G / denominator;
}
        
float3 SampleMicrofacetRay(float3 wo, float aT, float aB, float2 u)
{
    if (GGX::IsSmooth(aT, aB))
        return 0.0;
    
    float3 wh = GGX::SampleVisibleNormal(wo, aT, aB, u);
    float3 wi = reflect(-wo, wh);
            
    if (!SameHemisphere(wo, wi))
        return 0.0;
            
    return wi;      
}
               
} // namespace Reflection
    
    
// Reference: https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
namespace Clearcoat
{

float D_GTR1(float NoH, float alpha)
{
    float a2 = sq(clamp(alpha, 1.0e-3, 1.0));
    if (abs(a2 - 1.0) <= 1.0e-6)
        return 1.0 / PI;

    float c = (a2 - 1.0) / (PI * log(a2));
            
    return c / (1.0 + (a2 - 1.0) * NoH * NoH);
}

float EvaluatePDF(float3 wo, float3 wi, float alpha)
{
    if (!SameHemisphere(wo, wi))
        return 0.0;

    float3 wh = wo + wi;
    float hLenSq = dot(wh, wh);
    if (hLenSq <= EPSILON_MIN)
        return 0.0;
    wh *= rsqrt(hLenSq);

    float denominator = 4.0 * abs(dot(wo, wh));
    if (denominator <= 1.0e-6)
        return 0.0;
            
    float cosTheta = AbsCosTheta(wh);
    return D_GTR1(cosTheta, alpha) * cosTheta / denominator;
}
      
float3 EvaluateBRDF(float3 wo, float3 wi, float alpha)
{
    if (!SameHemisphere(wo, wi))
        return 0.0;
            
    float3 wh = wo + wi;
    float hLenSq = dot(wh, wh);
    if (hLenSq <= EPSILON_MIN)
        return 0.0;
    wh *= rsqrt(hLenSq);
    
    float  D = D_GTR1(AbsCosTheta(wh), alpha);
    float  G = GGX::G2(wo, wi, 0.25, 0.25);
    float3 F = Fresnel::Schlick(float3(0.04, 0.04, 0.04), saturate(dot(wo, wh)));
    return D * G * F / (4.0 * CosTheta(wo) * CosTheta(wi));
}

float3 SampleRay(float3 wo, float alpha, float2 u)
{
    float a2 = sq(clamp(alpha, 1.0e-3, 1.0));
    
    float phi       = 2.0 * PI * u.y;
    float cos2Theta = abs(a2 - 1.0) <= 1.0e-6
        ? 1.0 - u.x
        : (1.0 - pow(a2, 1.0 - u.x)) / (1.0 - a2);
    cos2Theta = saturate(cos2Theta);
            
    float cosTheta = safeSqrt(cos2Theta);
    float sinTheta = safeSqrt(1.0 - cos2Theta);
            
    float3 H = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    if (!SameHemisphere(wo, H))
        H = -H;
    
    float3 wi = reflect(-wo, H);
    return wi;
}

} // namespace Clearcoat
    
// Reference: https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf    
namespace Transmission
{

bool Refract(float3 wi, float3 n, float etaTOverI, out float3 wt, out float resolvedEtaTOverI)
{
    float cosThetaI = dot(n, wi);
    if (cosThetaI < 0.0)
    {
        n = -n;
        etaTOverI = 1.0 / etaTOverI;
        cosThetaI = -cosThetaI;
    }
    
    float sin2ThetaT = max(0.0, (1.0 - cosThetaI * cosThetaI)) / (etaTOverI * etaTOverI);
    if (sin2ThetaT >= 1.0)
    {
        // TIR
        wt = 0.0;
        resolvedEtaTOverI = etaTOverI;
        return false;
    }

    float cosThetaT = safeSqrt(1.0 - sin2ThetaT);

    wt = -wi / etaTOverI + (cosThetaI / etaTOverI - cosThetaT) * n;
    resolvedEtaTOverI = etaTOverI;
    return true;
}

float3 HalfVector(float3 wo, float3 wi, float etaTOverI, out float resolvedEtaTOverI)
{
    float cosThetaO = CosTheta(wo);

    resolvedEtaTOverI = 1.0;
    if (!SameHemisphere(wo, wi))
    {
        resolvedEtaTOverI = (cosThetaO > 0.0) ? etaTOverI : (1.0 / etaTOverI);
    }

    float3 wh = wi * resolvedEtaTOverI + wo;
    if (dot(wh, wh) == 0.0)
        return 0.0;

    wh = normalize(wh);
    return (wh.z > 0.0) ? wh : -wh;
}

float Jacobian(float3 wo, float3 wi, float3 wh, float resolvedEtaTOverI)
{
    float H2 = sq(dot(wi, wh) + dot(wo, wh) / resolvedEtaTOverI);
    if (H2 == 0.0)
        return 0.0;

    return abs(dot(wi, wh)) / H2;
}

bool IsTransmittable(float3 wo, float3 wi, float etaTOverI, out float3 wh, out float resolvedEtaTOverI)
{
    wh    = float3(0.0, 0.0, 0.0);
    resolvedEtaTOverI = 1.0;

    if (etaTOverI == 1.0)
        return false;

    if (SameHemisphere(wo, wi))
        return false;

    float cosThetaO = CosTheta(wo);
    float cosThetaI = CosTheta(wi);
    if (cosThetaO == 0.0 || cosThetaI == 0.0)
        return false;

    wh = HalfVector(wo, wi, etaTOverI, resolvedEtaTOverI);
    if (dot(wh, wh) == 0.0)
        return false;

    if (dot(wh,wi) * cosThetaI < 0.0 || dot(wh,wo) * cosThetaO < 0.0)
        return false; // back-facing

    return true;
}

float EvaluateMicrofacetPDF(float3 wo, float3 wi, float aT, float aB, float etaTOverI)
{
    if (GGX::IsSmooth(aT, aB))
        return 0.0;

    float resolvedEtaTOverI;
    float3 wh;
    if (!IsTransmittable(wo, wi, etaTOverI, wh, resolvedEtaTOverI))
        return 0.0;

    float D = GGX::D(wh, aT, aB);
    float G = GGX::G1(wo, aT, aB);
    float J = Jacobian(wo, wi, wh, resolvedEtaTOverI);
    return D * G * abs(dot(wo, wh)) * J / AbsCosTheta(wo);
}

float3 EvaluateMicrofacetBTDF(float3 wo, float3 wi, float3 oneMinusF, float aT, float aB, float etaTOverI, uint mode)
{
    if (GGX::IsSmooth(aT, aB))
        return 0.0;

    float  resolvedEtaTOverI;
    float3 wh;
    if (!IsTransmittable(wo, wi, etaTOverI, wh, resolvedEtaTOverI))
        return 0.0;

    float denominator = AbsCosTheta(wo) * AbsCosTheta(wi);
    if (denominator <= 1.0e-6)
        return 0.0;
    
    float D = GGX::D(wh, aT, aB);
    float G = GGX::G2(wo, wi, aT, aB);
    float J = Jacobian(wo, wi, wh, resolvedEtaTOverI);
    return D * G * oneMinusF * J * abs(dot(wo, wh)) * GetTransmissionScale(resolvedEtaTOverI, mode) / denominator;
}

} // namespace Transmission

} // namespace Lobe
    
    
// ── Material Models ─────────────────────────────    


namespace ScatteringModel
{

namespace Diffuse
{

float3 Evaluate(float3 wo, float3 wi, float3 albedo, float roughness, float scale, uint useBurley)
{
    if (!SameHemisphere(wo, wi) || scale <= 0.0)
        return float3(0.0, 0.0, 0.0);

    float3 f = useBurley != 0u
        ? Lobe::Diffuse::EvaluateBRDF(albedo, roughness, wo, wi)
        : Lobe::Diffuse::Lambert(albedo);
    return scale * f;
}

float EvaluatePDF(float3 wo, float3 wi)
{
    return Lobe::Diffuse::EvaluatePDF(wo, wi);
}

ScatteringModelSample Sample(float3 wo, float3 albedo, float roughness, float scale, uint useBurley, float2 u)
{
    ScatteringModelSample sample = (ScatteringModelSample)0;
    sample.wi  = Lobe::Diffuse::SampleRay(wo, u);
    sample.pdf = EvaluatePDF(wo, sample.wi);

    sample.weight  = Evaluate(wo, sample.wi, albedo, roughness, scale, useBurley) * AbsCosTheta(sample.wi) / sample.pdf;
    sample.lobe    = LOBE_DIFFUSE;
    sample.isDelta = 0u;
    return sample;
}

} // namespace Diffuse

namespace Sheen
{

float3 Evaluate(float3 wo, float3 wi, float3 sheenColor, float sheenRoughness, uint usePrincipled)
{
    if (!SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);

    if (usePrincipled == 0u)
        return Lobe::Sheen::EvaluateBRDF(sheenColor, sheenRoughness, wo, wi);

    float3 wh = wo + wi;
    float whLengthSq = dot(wh, wh);
    if (whLengthSq <= 0.0)
        return float3(0.0, 0.0, 0.0);

    wh *= rsqrt(whLengthSq);
    float sheenWeight = pow(saturate(1.0 - dot(wi, wh)), 5.0);
    return sheenColor * sheenWeight;
}

float EvaluatePDF(float3 wo, float3 wi)
{
    return Lobe::Diffuse::EvaluatePDF(wo, wi);
}

ScatteringModelSample Sample(float3 wo, float3 sheenColor, float sheenRoughness, uint usePrincipled, float2 u)
{
    ScatteringModelSample sample = (ScatteringModelSample)0;
    sample.wi  = Lobe::Diffuse::SampleRay(wo, u);
    sample.pdf = EvaluatePDF(wo, sample.wi);

    sample.weight  = Evaluate(wo, sample.wi, sheenColor, sheenRoughness, usePrincipled) * AbsCosTheta(sample.wi) / sample.pdf;
    sample.lobe    = LOBE_SHEEN;
    sample.isDelta = 0u;
    return sample;
}

} // namespace Sheen

namespace Clearcoat
{

float3 Evaluate(float3 wo, float3 wi, float alpha, float amount, uint usePrincipled)
{
    if (amount <= 0.0 || !SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);

    float3 f = Lobe::Clearcoat::EvaluateBRDF(wo, wi, alpha);
    if (usePrincipled == 0u)
        return amount * f;

    float disneyScale = 4.0 * AbsCosTheta(wo) * AbsCosTheta(wi);
    return (amount * 0.25) * disneyScale * f;
}

float EvaluatePDF(float3 wo, float3 wi, float alpha)
{
    return Lobe::Clearcoat::EvaluatePDF(wo, wi, alpha);
}

ScatteringModelSample Sample(float3 wo, float alpha, float amount, uint usePrincipled, float2 u)
{
    ScatteringModelSample sample = (ScatteringModelSample)0;
    sample.wi  = Lobe::Clearcoat::SampleRay(wo, alpha, u);
    sample.pdf = EvaluatePDF(wo, sample.wi, alpha);
    if (sample.pdf <= 0.0)
        return (ScatteringModelSample)0;

    sample.weight  = Evaluate(wo, sample.wi, alpha, amount, usePrincipled) * AbsCosTheta(sample.wi) / sample.pdf;
    sample.lobe    = LOBE_CLEARCOAT;
    sample.isDelta = 0u;
    return sample;
}

} // namespace Clearcoat

namespace Conductor
{

namespace Smooth
{
            
float3 EvaluateReflection(float3 wo, float3 F0)
{
    return Fresnel::Schlick(F0, AbsCosTheta(wo));
}
            
} // namespace Conductor::Smooth
   
float3 EvaluateReflection(float3 wo, float3 wi, float3 F0, float aT, float aB)
{
    if (GGX::IsSmooth(aT, aB))
        return 0.0;

    if (!SameHemisphere(wo, wi))
        return 0.0;
     
    float3 wh = wo + wi;
    if (dot(wh, wh) <= 1.0e-6)
        return 0.0;
    
    wh = normalize(wh);
    if (wh.z < 0.0)
        wh = -wh;
            
    float3 F = Fresnel::Schlick(F0, saturate(dot(wo, wh)));
    return Lobe::Reflection::EvaluateMicrofacetBRDF(wo, wi, F, aT, aB);
}

float3 Evaluate(float3 wo, float3 wi, float3 F0, float scale, float aT, float aB)
{
    return scale > 0.0 ? scale * EvaluateReflection(wo, wi, F0, aT, aB) : float3(0.0, 0.0, 0.0);
}

float EvaluatePDF(float3 wo, float3 wi, float aT, float aB)
{
    return Lobe::Reflection::EvaluateMicrofacetPDF(wo, wi, aT, aB);
}
float EvaluateDeltaPMF(float3 wo, float3 wi, float scale, float aT, float aB)
{
    if (scale <= 0.0 || !GGX::IsSmooth(aT, aB) || !SameHemisphere(wo, wi))
        return 0.0;

    float3 reflectedWi = float3(-wo.x, -wo.y, wo.z);
    return SameDirection(wi, reflectedWi) ? 1.0 : 0.0;
}



ScatteringModelSample Sample(float3 wo, float3 F0, float scale, float aT, float aB, float2 u)
{
    ScatteringModelSample sample = (ScatteringModelSample)0;
    if (scale <= 0.0)
        return sample;

    if (GGX::IsSmooth(aT, aB))
    {
        sample.wi      = float3(-wo.x, -wo.y, wo.z);
        sample.pdf     = 1.0;
        sample.weight  = scale * Smooth::EvaluateReflection(wo, F0);
        sample.isDelta = 1u;
    }
    else
    {
        sample.wi  = Lobe::Reflection::SampleMicrofacetRay(wo, aT, aB, u);
        sample.pdf = EvaluatePDF(wo, sample.wi, aT, aB);
        if (sample.pdf <= 0.0)
            return (ScatteringModelSample)0;

        sample.weight  = Evaluate(wo, sample.wi, F0, scale, aT, aB) * AbsCosTheta(sample.wi) / sample.pdf;
        sample.isDelta = 0u;
    }

    sample.lobe = LOBE_SPECULAR;
    return sample;
}


} // namespace Conductor

namespace Dielectric
{

float3 EvaluateFresnel(float cosTheta, float3 reflectionF0, float3 reflectionF90, float etaTOverI)
{
    float eta = max(etaTOverI, 1.0e-4);
    if (abs(eta - 1.0) <= EPSILON_MIN)
        return float3(0.0, 0.0, 0.0);

    float baseF0 = sq((eta - 1.0) / (eta + 1.0));
    float exactF = Fresnel::Dielectric(cosTheta, 1.0, eta);

    float3 boundedF90 = saturate(reflectionF90);
    float3 boundedF0  = min(saturate(reflectionF0), boundedF90);
    float normalizedF = baseF0 < 1.0 - 1.0e-6
        ? saturate((exactF - baseF0) / max(1.0 - baseF0, 1.0e-6))
        : 1.0;
    return lerp(boundedF0, boundedF90, normalizedF);
}

namespace Smooth
{

float3 EvaluateReflection(
    float3 wo,
    float3 reflectionF0,
    float3 reflectionF90,
    float etaTOverI)
{
    return EvaluateFresnel(CosTheta(wo), reflectionF0, reflectionF90, etaTOverI);
}

} // namespace Dielectric::Smooth

    
float3 EvaluateReflection(
    float3 wo,
    float3 wi,
    float3 reflectionScale,
    float3 reflectionF0,
    float3 reflectionF90,
    float aT,
    float aB,
    float etaTOverI)
{
    if (GGX::IsSmooth(aT, aB))
        return 0.0;
            
    if (!SameHemisphere(wo, wi))
        return 0;
     
    float3 wh = wo + wi;
    if (dot(wh, wh) <= 1.0e-6)
        return 0;
    
    wh = normalize(wh);
    if (wh.z < 0.0)
        wh = -wh;
            
    float3 F = EvaluateFresnel(dot(wo, wh), reflectionF0, reflectionF90, etaTOverI);
    return reflectionScale * Lobe::Reflection::EvaluateMicrofacetBRDF(wo, wi, F, aT, aB);
}
    
float3 EvaluateTransmission(
    float3 wo,
    float3 wi,
    float3 transmissionScale,
    float3 reflectionF0,
    float3 reflectionF90,
    float aT,
    float aB,
    float etaTOverI,
    uint mode)
{
    if (GGX::IsSmooth(aT, aB))
        return 0.0;
           
    float  resolvedEtaTOverI;
    float3 wh;
    if (!Lobe::Transmission::IsTransmittable(wo, wi, etaTOverI, wh, resolvedEtaTOverI))
        return 0.0;
            
    float3 F = EvaluateFresnel(dot(wo, wh), reflectionF0, reflectionF90, etaTOverI);
    return transmissionScale * Lobe::Transmission::EvaluateMicrofacetBTDF(wo, wi, 1.0 - F, aT, aB, etaTOverI, mode);
}

bool TryReflect(float3 wo, float3 wh, out float3 wi)
{
    wi = reflect(-wo, wh);
    return SameHemisphere(wo, wi);
}

// R/T branch selection pmf
float2 ResolveBranchPMF(
    float3 wo,
    float3 wh,
    float3 reflectionScale,
    float3 transmissionScale,
    float3 reflectionF0,
    float3 reflectionF90,
    float etaTOverI,
    uint canReflect,
    uint canTransmit)
{
    float3 F = EvaluateFresnel(dot(wo, wh), reflectionF0, reflectionF90, etaTOverI);

    float reflectionWeight   = canReflect != 0u ? max3(max(reflectionScale * F, float3(0.0, 0.0, 0.0))) : 0.0;
    float transmissionWeight = canTransmit != 0u ? max3(max(transmissionScale * (1.0 - F), float3(0.0, 0.0, 0.0))) : 0.0;
    float weightSum          = reflectionWeight + transmissionWeight;

    return weightSum > EPSILON_MIN ? float2(reflectionWeight, transmissionWeight) / weightSum : float2(0.0, 0.0);
}

float EvaluatePDF(
    float3 wo,
    float3 wi,
    float3 reflectionScale,
    float3 transmissionScale,
    float3 reflectionF0,
    float3 reflectionF90,
    float aT,
    float aB,
    float etaTOverI)
{
    if (GGX::IsSmooth(aT, aB) || abs(etaTOverI - 1.0) <= EPSILON_MIN)
        return 0.0;

    bool queryReflection = SameHemisphere(wo, wi);
    float3 wh;
    float resolvedEtaTOverI;
    if (queryReflection)
    {
        wh = wo + wi;
        float whLengthSq = dot(wh, wh);
        if (whLengthSq <= EPSILON_MIN)
            return 0.0;
        wh *= rsqrt(whLengthSq);
        if (wh.z < 0.0)
            wh = -wh;
    }
    else if (!Lobe::Transmission::IsTransmittable(wo, wi, etaTOverI, wh, resolvedEtaTOverI))
    {
        return 0.0;
    }

    float3 reflectedWi;
    bool canReflect = TryReflect(wo, wh, reflectedWi);

    float3 transmittedWi;
    bool canTransmit = Lobe::Transmission::Refract(wo, wh, etaTOverI, transmittedWi, resolvedEtaTOverI) &&
                       !SameHemisphere(wo, transmittedWi);

    float2 branchPMF = ResolveBranchPMF(
        wo,
        wh,
        reflectionScale,
        transmissionScale,
        reflectionF0,
        reflectionF90,
        etaTOverI,
        canReflect ? 1u : 0u,
        canTransmit ? 1u : 0u);

    return queryReflection
        ? branchPMF.x * Lobe::Reflection::EvaluateMicrofacetPDF(wo, wi, aT, aB)
        : branchPMF.y * Lobe::Transmission::EvaluateMicrofacetPDF(wo, wi, aT, aB, etaTOverI);
}

float EvaluateTransmissionPDF(float3 wo, float3 wi, float aT, float aB, float etaTOverI)
{
    if (GGX::IsSmooth(aT, aB) || abs(etaTOverI - 1.0) <= EPSILON_MIN)
        return 0.0;

    return Lobe::Transmission::EvaluateMicrofacetPDF(wo, wi, aT, aB, etaTOverI);
}

float EvaluateDeltaPMF(
    float3 wo,
    float3 wi,
    float3 reflectionScale,
    float3 transmissionScale,
    float3 reflectionF0,
    float3 reflectionF90,
    float aT,
    float aB,
    float etaTOverI)
{
    bool isDelta = GGX::IsSmooth(aT, aB) || abs(etaTOverI - 1.0) <= EPSILON_MIN;
    if (!isDelta)
        return 0.0;

    float3 wh = float3(0.0, 0.0, 1.0);
    float3 reflectedWi;
    bool canReflect = TryReflect(wo, wh, reflectedWi);

    float resolvedEtaTOverI;
    float3 transmittedWi;
    bool canTransmit = Lobe::Transmission::Refract(wo, wh, etaTOverI, transmittedWi, resolvedEtaTOverI) &&
                       !SameHemisphere(wo, transmittedWi);

    float2 branchPMF = ResolveBranchPMF(
        wo,
        wh,
        reflectionScale,
        transmissionScale,
        reflectionF0,
        reflectionF90,
        etaTOverI,
        canReflect ? 1u : 0u,
        canTransmit ? 1u : 0u);

    bool queryReflection = SameHemisphere(wo, wi);
    if (queryReflection)
        return canReflect && SameDirection(wi, reflectedWi) ? branchPMF.x : 0.0;

    if (canTransmit && SameDirection(wi, transmittedWi))
        return branchPMF.y;
    return 0.0;
}

ScatteringModelSample SampleTransmission(
    float3 wo,
    float3 transmissionScale,
    float3 reflectionF0,
    float3 reflectionF90,
    float aT,
    float aB,
    float etaTOverI,
    uint mode,
    float2 u)
{
    ScatteringModelSample sample = (ScatteringModelSample)0;
    bool isDelta = GGX::IsSmooth(aT, aB) || abs(etaTOverI - 1.0) <= EPSILON_MIN;
    float3 wh = isDelta
        ? float3(0.0, 0.0, 1.0)
        : GGX::SampleVisibleNormal(wo, aT, aB, u);

    float resolvedEtaTOverI;
    if (!Lobe::Transmission::Refract(wo, wh, etaTOverI, sample.wi, resolvedEtaTOverI) ||
        SameHemisphere(wo, sample.wi))
    {
        return (ScatteringModelSample)0;
    }

    if (isDelta)
    {
        float3 F = EvaluateFresnel(CosTheta(wo), reflectionF0, reflectionF90, etaTOverI);
        sample.pdf     = 1.0;
        sample.weight  = transmissionScale * (1.0 - F) *
                         GetTransmissionScale(resolvedEtaTOverI, mode);
        sample.isDelta = 1u;
    }
    else
    {
        sample.pdf = Lobe::Transmission::EvaluateMicrofacetPDF(
            wo, sample.wi, aT, aB, etaTOverI);
        if (sample.pdf <= 0.0)
            return (ScatteringModelSample)0;

        sample.weight = EvaluateTransmission(
            wo,
            sample.wi,
            transmissionScale,
            reflectionF0,
            reflectionF90,
            aT,
            aB,
            etaTOverI,
            mode) * AbsCosTheta(sample.wi) / sample.pdf;
    }
    sample.lobe = LOBE_TRANSMISSION;
    return sample;
}

ScatteringModelSample Sample(
    float3 wo,
    float3 reflectionScale,
    float3 transmissionScale,
    float3 reflectionF0,
    float3 reflectionF90,
    float aT,
    float aB,
    float etaTOverI,
    uint mode,
    float3 u)
{
    ScatteringModelSample sample = (ScatteringModelSample)0;
    bool isDelta = GGX::IsSmooth(aT, aB) || abs(etaTOverI - 1.0) <= EPSILON_MIN;

    float3 wh = isDelta ? float3(0.0, 0.0, 1.0) : GGX::SampleVisibleNormal(wo, aT, aB, u.xy);

    float3 reflectedWi;
    bool canReflect = TryReflect(wo, wh, reflectedWi);

    float resolvedEtaTOverI;
    float3 transmittedWi;
    bool canTransmit = Lobe::Transmission::Refract(wo, wh, etaTOverI, transmittedWi, resolvedEtaTOverI) &&
                       !SameHemisphere(wo, transmittedWi);

    float2 branchPMF = ResolveBranchPMF(
        wo,
        wh,
        reflectionScale,
        transmissionScale,
        reflectionF0,
        reflectionF90,
        etaTOverI,
        canReflect ? 1u : 0u,
        canTransmit ? 1u : 0u);
    if (branchPMF.x + branchPMF.y <= 0.0)
        return sample;

    bool chooseReflection = branchPMF.x > 0.0 && (branchPMF.y <= 0.0 || u.z < branchPMF.x);
    if (chooseReflection)
    {
        sample.wi   = reflectedWi;
        sample.pdf  = branchPMF.x;
        sample.lobe = LOBE_SPECULAR;

        if (isDelta)
        {
            sample.weight  = reflectionScale * Smooth::EvaluateReflection(wo, reflectionF0, reflectionF90, etaTOverI) / sample.pdf;
            sample.isDelta = 1u;
        }
        else
        {
            sample.pdf *= Lobe::Reflection::EvaluateMicrofacetPDF(wo, sample.wi, aT, aB);
            if (sample.pdf <= 0.0)
                return (ScatteringModelSample)0;
            sample.weight = EvaluateReflection(
                wo, sample.wi, reflectionScale, reflectionF0, reflectionF90, aT, aB, etaTOverI) *
                AbsCosTheta(sample.wi) / sample.pdf;
        }
    }
    else
    {
        sample.wi   = transmittedWi;
        sample.pdf  = branchPMF.y;
        sample.lobe = LOBE_TRANSMISSION;

        if (isDelta)
        {
            float3 F = EvaluateFresnel(CosTheta(wo), reflectionF0, reflectionF90, etaTOverI);
            sample.weight  = transmissionScale * (1.0 - F) *
                             GetTransmissionScale(resolvedEtaTOverI, mode) / sample.pdf;
            sample.isDelta = 1u;
        }
        else
        {
            sample.pdf *= Lobe::Transmission::EvaluateMicrofacetPDF(wo, sample.wi, aT, aB, etaTOverI);
            if (sample.pdf <= 0.0)
                return (ScatteringModelSample)0;
            sample.weight = EvaluateTransmission(
                wo, sample.wi, transmissionScale, reflectionF0, reflectionF90, aT, aB, etaTOverI, mode) *
                AbsCosTheta(sample.wi) / sample.pdf;
        }
    }

    return sample;
}

        
namespace Thin
{

float Reflectance(float3 wo, float sheetEtaTOverI)
{
    float F = Fresnel::Dielectric(AbsCosTheta(wo), 1.0, sheetEtaTOverI);
    return (2.0 * F) / (1.0 + F);
}

float2 ResolveBranchPMF(float3 wo, float3 reflectionScale, float3 transmissionScale, float sheetEtaTOverI)
{
    float R = Reflectance(wo, sheetEtaTOverI);
    float reflectionWeight = max3(max(reflectionScale, float3(0.0, 0.0, 0.0))) * R;
    float transmissionWeight = max3(max(transmissionScale, float3(0.0, 0.0, 0.0))) * (1.0 - R);
    float weightSum = reflectionWeight + transmissionWeight;
    return weightSum > EPSILON_MIN
        ? float2(reflectionWeight, transmissionWeight) / weightSum
        : float2(0.0, 0.0);
}

float EvaluateDeltaPMF(float3 wo, float3 wi, float3 reflectionScale, float3 transmissionScale, float sheetEtaTOverI)
{
    float2 branchPMF   = ResolveBranchPMF(wo, reflectionScale, transmissionScale, sheetEtaTOverI);
    float3 reflectedWi = float3(-wo.x, -wo.y, wo.z);

    bool queryReflection = SameHemisphere(wo, wi);
    if (queryReflection)
        return SameDirection(wi, reflectedWi) ? branchPMF.x : 0.0;

    if (SameDirection(wi, -wo))
        return branchPMF.y;
    return 0.0;
}

ScatteringModelSample Sample(float3 wo, float3 reflectionScale, float3 transmissionScale, float sheetEtaTOverI, float uc)
{
    ScatteringModelSample sample = (ScatteringModelSample)0;

    float  R         = Reflectance(wo, sheetEtaTOverI);
    float2 branchPMF = ResolveBranchPMF(wo, reflectionScale, transmissionScale, sheetEtaTOverI);
    if (branchPMF.x + branchPMF.y <= 0.0)
        return sample;

    bool chooseReflection = branchPMF.x > 0.0 && (branchPMF.y <= 0.0 || uc < branchPMF.x);
    if (chooseReflection)
    {
        sample.wi     = float3(-wo.x, -wo.y, wo.z);
        sample.pdf    = branchPMF.x;
        sample.weight = reflectionScale * R / sample.pdf;
        sample.lobe   = LOBE_SPECULAR;
    }
    else
    {
        sample.wi     = -wo;
        sample.pdf    = branchPMF.y;
        sample.weight = transmissionScale * (1.0 - R) / sample.pdf;
        sample.lobe   = LOBE_TRANSMISSION;
    }

    sample.isDelta = 1u;
    return sample;
}

} // namespace Thin

} // namespace Dielectric


} // namespace ScatteringModel
    
}  // namespace BxDF

#endif // _HLSL_BXDF_HEADER
