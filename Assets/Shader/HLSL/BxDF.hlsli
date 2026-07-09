#ifndef _HLSL_BXDF_HEADER
#define _HLSL_BXDF_HEADER

#include "Common.hlsli"
#include "HelperFunctions.hlsli"

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

struct SurfaceParameters
{
    float3 P;
    float3 Ng;    // geometric normal (world); used for ray offset
    Frame  frame; // tangent-space basis at P

    float3 baseColor;
    float  metallic;
    float  roughness;
    float  ior;

    float  clearcoat;
    float  clearcoatRoughness;
    float  transmission;
    float3 sheenColor;
    float  sheenRoughness;
    float3 specularColor;
    float  specularStrength;
};

struct BSDFSample
{
    float3 wi;
    float3 weight;
    float  pdf;
    uint   lobe;
    uint   isDelta; // is direc-delta lobe
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
float Sin2Theta   (float3 w) { return max(0.0, 1.0 - Cos2Theta(w)); }

bool SameHemisphere(float3 wo, float3 wi) { return wo.z * wi.z > 0.0; }

// ── World ↔ Local conversion helpers ─────────────────────────────────────────────
float3 ToLocal(Frame f, float3 vW)
{
    return float3(dot(vW, f.T), dot(vW, f.B), dot(vW, f.N));
}

float3 ToWorld(Frame f, float3 vL)
{
    return f.T * vL.x + f.B * vL.y + f.N * vL.z;
}


// ── Lobes ─────────────────────────────
    
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
float Dielectric(float cosThetaI, float iorI, float iorT)
{
    float eta = iorT / iorI;
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);

    // Back face: ray exiting denser side. Swap so the math runs as "entering".
    if (cosThetaI < 0.0)
    {
        eta = 1.0 / eta;
        cosThetaI = -cosThetaI;
    }

    // Snell: sinθ_t = (η_i / η_t) · sinθ_i.
    float sinThetaI = sqrt(max(0.0, 1.0 - cosThetaI * cosThetaI));
    float sinThetaT = sinThetaI * (1.0 / eta);

    // TIR: full reflection (no transmittance).
    if (sinThetaT >= 1.0)
        return 1.0;
            
    float cosThetaT = safeSqrt(1.0 - sinThetaT * sinThetaT);
            
    float Rparl = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
    float Rperp = (cosThetaI - eta * cosThetaT) / (cosThetaI + eta * cosThetaT);

    return (Rparl * Rparl + Rperp * Rperp) / 2.0;
}

} // namespace Fresnel


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


namespace GGX
{

// Reference: https://schuttejoe.github.io/post/disneybsdf/
float2 ComputeAnisotropicAlpha(float roughness, float anisotropy)
{
    float a = roughness * roughness;
    float s = sqrt(max(0.0, 1.0 - 0.9 * anisotropy));
    return float2(a / max(s, 1e-4), a * s);   // (αT, αB)
}

float D(float3 h, float aT, float aB)
{
    float d = sq(h.x / aT) + sq(h.y / aB) + sq(h.z);
    return 1.0 / (PI * aT * aB * sq(d));
}

float Lambda(float3 w, float aT, float aB)
{
    float a2Inv = (sq(aT * w.x) + sq(aB * w.y));
    return (sqrt(1.0 + a2Inv / sq(w.z)) - 1.0) * 0.5;
}

float G1(float3 w, float aT, float aB)
{
    return 1.0 / (1.0 + Lambda(w, aT, aB));
}
        
float G2(float3 wo, float3 wi, float aT, float aB)
{
    return 1.0 / (1.0 + Lambda(wo, aT, aB) + Lambda(wi, aT, aB));
}

float EvaluatePDF(float3 wo, float3 wi, float aT, float aB)
{
    if (!SameHemisphere(wo, wi))
        return 0.0;
    
    float3 wh = normalize(wi + wo);
            
    float D_ = D(wh, aT, aB);
    float G  = G1(wo, aT, aB);
    
    return D_ * G / (4.0 * AbsCosTheta(wo));
}

float3 EvaluateBRDF(float3 wo, float3 wi, float3 F0, float aT, float aB)
{
    if (!SameHemisphere(wo, wi))
        return float3(0.0, 0.0, 0.0);
    
    float3 H = normalize(wi + wo);
    
    float  D_ = D(H, aT, aB);
    float  G  = G2(wo, wi, aT, aB);
    float3 F  = Fresnel::Schlick(F0, saturate(dot(wo, H)));
            
    return F * D_ * G / (4.0 * CosTheta(wo) * CosTheta(wi));
}

// Reference: https://jcgt.org/published/0007/04/01/paper.pdf
float3 SampleRay(float3 wo, float aT, float aB, float2 u)
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


// Reference: https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
namespace Clearcoat
{

float D_GTR1(float NoH, float alpha)
{
    float a2 = alpha * alpha;
    float c  = (a2 - 1.0) / (PI * log(a2));
            
    return c / (1.0 + (a2 - 1.0) * NoH * NoH);
}

float EvaluatePDF(float3 wo, float3 wi, float alpha)
{
    if (!SameHemisphere(wo, wi))
        return 0.0;

    float3 H = normalize(wo + wi);

    float cosTheta = AbsCosTheta(H);
    return D_GTR1(cosTheta, alpha) * cosTheta / (4.0 * abs(dot(wo, H)));
}
      
float3 EvaluateBRDF(float3 wo, float3 wi, float alpha)
{
    if (!SameHemisphere(wo, wi))
        return 0.0;
            
    float3 H = normalize(wo + wi);
    
    float  D = D_GTR1(AbsCosTheta(H), alpha);
    float  G = GGX::G2(wo, wi, 0.25, 0.25);
    float3 F = Fresnel::Schlick(float3(0.04, 0.04, 0.04), saturate(dot(wo, H)));
    return D * G * F / (4.0 * CosTheta(wo) * CosTheta(wi));
}

float3 SampleRay(float3 wo, float alpha, float2 u)
{
    float a2 = alpha * alpha;
    
    float phi       = 2.0 * PI * u.y;
    float cos2Theta = (1.0 - pow(a2, 1.0 - u.x)) / (1.0 - a2);
            
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
namespace Dielectric
{

bool IsSmooth(float aT, float aB)
{
    return max(aT, aB) < 1.0e-3;
}

bool Refract(float3 wi, float3 n, float eta, out float3 wt, out float eta_p)
{
    float cosThetaI = dot(n, wi);
    if (cosThetaI < 0.0)
    {
        n = -n;
        eta = 1.0 / eta;
        cosThetaI = -cosThetaI;
    }
    
    float sin2ThetaT = max(0.0, (1.0 - cosThetaI * cosThetaI)) / (eta * eta);
    if (sin2ThetaT >= 1.0)
    {
        // TIR
        wt = 0.0;
        eta_p = eta;
        return false;
    }

    float cosThetaT = safeSqrt(1.0 - sin2ThetaT);

    wt   = -wi / eta + (cosThetaI / eta - cosThetaT) * n;
    eta_p = eta;
    return true;
}

float3 HalfVector(float3 wo, float3 wi, float eta, out float eta_p)
{
    float cosThetaO = CosTheta(wo);

    eta_p = 1.0;
    if (!SameHemisphere(wo, wi))
    {
        eta_p = (cosThetaO > 0.0) ? eta : (1.0 / eta);
    }

    float3 wh = wi * eta_p + wo;
    if (dot(wh, wh) == 0.0)
        return 0.0;

    wh = normalize(wh);
    return (wh.z > 0.0) ? wh : -wh;
}

float Jacobian(float3 wo, float3 wi, float3 wh, float eta_p)
{
    float H2 = sq(dot(wi, wh) + dot(wo, wh) / eta_p);
    if (H2 == 0.0)
        return 0.0;

    return abs(dot(wi, wh)) / H2;
}

bool IsTransmittable(float3 wo, float3 wi, float eta, out float3 wh, out float eta_p)
{
    wh    = float3(0.0, 0.0, 0.0);
    eta_p = 1.0;

    if (eta == 1.0)
        return false;

    if (SameHemisphere(wo, wi))
        return false;

    float cosThetaO = CosTheta(wo);
    float cosThetaI = CosTheta(wi);
    if (cosThetaO == 0.0 || cosThetaI == 0.0)
        return false;

    wh = HalfVector(wo, wi, eta, eta_p);
    if (dot(wh, wh) == 0.0)
        return false;

    if (dot(wh,wi) * cosThetaI < 0.0 || dot(wh,wo) * cosThetaO < 0.0)
        return false; // back-facing

    return true;
}

float EvaluatePDF(float3 wo, float3 wi, float aT, float aB, float eta)
{
    if (IsSmooth(aT, aB))
        return 0.0;

    float eta_p;
    float3 wh;
    if (!IsTransmittable(wo, wi, eta, wh, eta_p))
        return 0.0;

    float D = GGX::D(wh, aT, aB);
    float G = GGX::G1(wo, aT, aB);
    float J = Jacobian(wo, wi, wh, eta_p);
    return D * G * abs(dot(wo, wh)) * J / abs(CosTheta(wo));
}
        
float3 EvaluateBRDF(float3 wo, float3 wi, float aT, float aB, float eta)
{
    if (IsSmooth(aT, aB))
        return 0.0;

    if (!SameHemisphere(wo, wi))
        return 0.0;

    float3 wh = wo + wi;
    if (dot(wh, wh) == 0.0)
        return 0.0;

    wh = normalize(wh);
    if (wh.z < 0.0)
        wh = -wh;

    float D = GGX::D(wh, aT, aB);
    float G = GGX::G2(wo, wi, aT, aB);
    float F = Fresnel::Dielectric(dot(wo, wh), 1.0, eta);
    float denom = 4.0 * AbsCosTheta(wo) * AbsCosTheta(wi);
    if (denom <= 0.0)
        return 0.0;

    return float3(F, F, F) * D * G / denom;
}

float3 EvaluateBTDF(float3 wo, float3 wi, float aT, float aB, float eta)
{
    if (IsSmooth(aT, aB))
        return 0.0;

    float  eta_p;
    float3 wh;
    if (!IsTransmittable(wo, wi, eta, wh, eta_p))
        return 0.0;

    float D = GGX::D(wh, aT, aB);
    float G = GGX::G2(wo, wi, aT, aB);
    float F = Fresnel::Dielectric(dot(wo, wh), 1.0, eta);
    float J = Jacobian(wo, wi, wh, eta_p);
    return D * G * (1.0 - F) * J * abs(dot(wo, wh)) / (abs(CosTheta(wo) * CosTheta(wi)) * sq(eta_p));
}

float3 EvaluateBSDF(float3 wo, float3 wi, float aT, float aB, float eta)
{
    return SameHemisphere(wo, wi) ? EvaluateBRDF(wo, wi, aT, aB, eta) : EvaluateBTDF(wo, wi, aT, aB, eta);
}

BSDFSample SampleRay(float3 wo, float aT, float aB, float eta, float uc, float2 u)
{
    BSDFSample bs;
    bs.wi      = float3(0.0, 0.0, 0.0);
    bs.pdf     = 0.0;                 // default == INVALID
    bs.weight  = float3(0.0, 0.0, 0.0);
    bs.lobe    = LOBE_TRANSMISSION;
    bs.isDelta = 0u;                  // overridden to 1u in the smooth branch below

    bool isSmooth = (eta == 1.0) || IsSmooth(aT, aB);
    if (isSmooth)
    {
        float R = Fresnel::Dielectric(CosTheta(wo), 1.0, eta);
        float T = 1.0 - R;

        if (uc < R / (R + T))
        {
            // reflect branch
            float3 wi = float3(-wo.x, -wo.y, wo.z);
            float  f  = R / AbsCosTheta(wi);

            bs.wi      = wi;
            bs.pdf     = R / (R + T);
            bs.weight  = f * AbsCosTheta(wi) / bs.pdf;
            bs.lobe    = LOBE_SPECULAR;
            bs.isDelta = 1u;
        }
        else
        {
            // transmission branch
            float  eta_p;
            float3 wi;
            if (!Refract(wo, float3(0.0, 0.0, 1.0), eta, wi, eta_p))
                return bs;

            float f = T / (AbsCosTheta(wi) * eta_p * eta_p);

            bs.wi      = wi;
            bs.pdf     = T / (R + T);
            bs.weight  = f * AbsCosTheta(wi) / bs.pdf;
            bs.lobe    = LOBE_TRANSMISSION;
            bs.isDelta = 1u;
        }

        return bs;
    }

    // rough surface branch
    float3 wh = GGX::SampleRay(wo,aT,aB,u);

    float R = Fresnel::Dielectric(dot(wo, wh), 1.0, eta);
    float T = 1.0 - R;
    if (uc < R / (R + T))
    {
        // reflect branch
        float3 wi = reflect(-wo, wh);
        if (!SameHemisphere(wo, wi))
            return bs;

        float D  = GGX::D(wh, aT, aB);
        float G2 = GGX::G2(wo, wi, aT, aB);
        float G1 = GGX::G1(wo, aT, aB);

        float3 f = D * G2 * R / (4.0 * CosTheta(wo) * CosTheta(wi));

        bs.wi     = wi;
        bs.pdf    = D * G1 / (4.0 * AbsCosTheta(wo)) * (R / (R + T));
        bs.weight = f * AbsCosTheta(wi) / bs.pdf;
        bs.lobe   = LOBE_SPECULAR;
    }
    else
    {
        // transmission branch
        float  eta_p;
        float3 wi;
        if (!Refract(wo, wh, eta, wi, eta_p))
            return bs;

        if (wi.z == 0.0)
            return bs;

        if (SameHemisphere(wo, wi))
            return bs;

        float D  = GGX::D(wh, aT, aB);
        float G2 = GGX::G2(wo, wi, aT, aB);
        float G1 = GGX::G1(wo, aT, aB);
        float J  = Jacobian(wo, wi, wh, eta_p);

        float3 f = D * G2 * T * abs(dot(wo, wh)) * J / (abs(CosTheta(wo) * CosTheta(wi)) * sq(eta_p));

        bs.wi     = wi;
        bs.pdf    = D * G1 * abs(dot(wo, wh)) * J / AbsCosTheta(wo) * (T / (R + T));
        bs.weight = f * AbsCosTheta(wi) / bs.pdf;
        bs.lobe   = LOBE_TRANSMISSION;
    }

    return bs;
}

} // namespace Dielectric

}  // namespace BxDF

#endif // _HLSL_BXDF_HEADER
