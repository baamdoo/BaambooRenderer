//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// A family of BRDF, BSDF and BTDF functions.
// BRDF, BSDF, BTDF - bidirectional reflectance, scattering, transmission distribution function.
// Ref: Ray Tracing from the Ground Up (RTG), Suffern
// Ref: Real-Time Rendering (RTR), Fourth Edition
// Ref: Real Shading in Unreal Engine 4 (Karis_UE4), Karis2013
// Ref: PBR Diffuse Lighting for GGX+Smith Microsurfaces, Hammon2017

// BRDF terms generally include 1 / PI factor, but this is removed in the implementations below as it cancels out
// with the omitted PI factor in the reflectance equation. Ref: eq 9.14, RTR

// Parameters:
// iorIn - ior of media ray is coming from
// iorOut - ior of media ray is going to
// eta - relative index of refraction, namely iorIn / iorOut
// G - shadowing/masking function.
// Fo - specular reflectance at normal incidence (AKA specular color).
// Albedo - material color
// Roughness - material roughness
// N - surface normal
// V - direction to viewer
// L - incoming "to-light" direction
// T - transmission scale factor (aka transmission color)
// thetai - incident angle

#ifndef BXDF_HLSL
#define BXDF_HLSL
#include "Common.hlsli"

namespace BxDF 
{

bool IsBlack(float3 color)
{
    return !any(color);
}
    
// Fresnel reflectance - schlick approximation.
float3 Fresnel(in float3 F0, in float cosTheta)
{
    return F0 + (1 - F0) * pow(1 - cosTheta, 5);
}

float3 ComputeF0(float3 albedo, float metallic, float ior)
{
    float  f0           = pow((ior - 1.0) / (ior + 1.0), 2.0);
    float3 dielectricF0 = float3(f0, f0, f0);

    return lerp(dielectricF0, albedo, metallic);
}

namespace Diffuse 
{

    namespace Lambert 
    {
            
    float3 F(in float3 albedo)
    {
        return albedo;
    }
            
    }

    namespace Hammon 
    {
            
    // Compute the value of BRDF
    // Ref: Hammon2017
    float3 F(in float3 Albedo, in float Roughness, in float3 N, in float3 V, in float3 L, in float3 Fo)
    {
        float3 diffuse = 0;

        float3 H   = normalize(V + L);
        float  NoH = dot(N, H);
        if (NoH > 0)
        {
            float a = Roughness * Roughness;

            float NoV = saturate(dot(N, V));
            float NoL = saturate(dot(N, L));
            float VoL = saturate(dot(V, L));

            float  facing = 0.5 + 0.5 * VoL;
            float  rough  = facing * (0.9 - 0.4 * facing) * ((0.5 + NoH) / NoH);
            float3 smooth = 1.05 * (1 - pow(1 - NoL, 5)) * (1 - pow(1 - NoV, 5));

            // Extract 1 / PI from the single equation since it's ommited in the reflectance function.
            float  multi  = 0.3641 * a; // 0.3641 = PI * 0.1159
            float3 single = lerp(smooth, rough, a);

            diffuse = Albedo * (single + Albedo * multi);
        }
        return diffuse;
    }
            
    }
        
}

namespace Specular 
{

    // Perfect/Specular reflection.
    namespace Reflection 
    {
    
    // Calculates L and returns BRDF value for that direction.
    // Assumptions: V and N are in the same hemisphere.
    // Note: to avoid unnecessary precision issues and for the sake of performance the function doesn't divide by the cos term
    // so as to nullify the cos term in the rendering equation. Therefore the caller should skip the cos term in the rendering equation.
    float3 Sample_Fr(in float3 V, out float3 L, in float3 N, in float3 Fo)
    {
        L = reflect(-V, N);
        float cos_thetai = dot(N, L);
        return Fresnel(Fo, cos_thetai);
    }
    
    // Calculate whether a total reflection occurs at a given V and a normal
    // Ref: eq 27.5, Ray Tracing from the Ground Up
    bool IsTotalInternalReflection(
        in float3 V,
        in float3 normal)
    {
        float ior = 1; 
        float eta = ior;
        float cos_thetai = dot(normal, V); // Incident angle

        return 1 - 1 * (1 - cos_thetai * cos_thetai) / (eta * eta) < 0;
    }

    }

    // Perfect/Specular trasmission.
    namespace Transmission 
    {

    // Calculates transmitted ray wt and return BRDF value for that direction.
    // Assumptions: V and N are in the same hemisphere.
    // Note: to avoid unnecessary precision issues and for the sake of performance the function doesn't divide by the cos term
    // so as to nullify the cos term in the rendering equation. Therefore the caller should skip the cos term in the rendering equation.
    float3 Sample_Ft(in float3 V, out float3 wt, in float3 N, in float3 Fo)
    {
        float ior = 1;
        wt = -V; // TODO: refract(-V, N, ior);
        float cos_thetai = dot(V, N);
        float3 Kr = Fresnel(Fo, cos_thetai);

        return (1 - Kr);
    }
            
    }

    // Ref: Chapter 9.8, RTR
    namespace GGX 
    {

        // Compute the value of BRDF
        float3 F(in float Roughness, in float3 N, in float3 V, in float3 L, in float3 Fo)
        {
            float3 H = V + L;
            float NoL = dot(N, L);
            float NoV = dot(N, V);
            float3 specular = 0;

            if (NoL > 0 && NoV > 0 && all(H))
            {
                H = normalize(H);
                float a = Roughness;        // The roughness has already been remapped to alpha.
                float3 M = H;               // microfacet normal, equals h, since BRDF is 0 for all m =/= h. Ref: 9.34, RTR
                float NoM = saturate(dot(N, M));
                float HoL = saturate(dot(H, L));

                // D
                // Ref: eq 9.41, RTR
                float denom = 1 + NoM * NoM * (a * a - 1);
                float D = a * a / (denom * denom);  // Karis

                // F
                // Fresnel reflectance - Schlick approximation for F(h,l)
                // Ref: 9.16, RTR
                float3 F = Fresnel(Fo , HoL);

                // G
                // Visibility due to shadowing/masking of a microfacet.
                // G coupled with BRDF's {1 / 4 DotNL * DotNV} factor.
                // Ref: eq 9.45, RTR
                float G =  0.5 / lerp(2 * NoL * NoV, NoL + NoV, a);

                // Specular BRDF term
                // Ref: eq 9.34, RTR
                specular = F * G * D;
            }

            return specular;
        }
    }
}
    
namespace Clearcoat
{

    float V_Kelemen(float LoH)
    {
        return 0.25 / max(LoH * LoH, 1e-4);
    }
        
    float3 Evaluate(
        in float clearcoatFactor,
        in float clearcoatRoughness,
        in float3 N,
        in float3 V,
        in float3 L,
        out float attenuation)
    {
        attenuation = 1.0;
        if (clearcoatFactor <= 0.0)
            return float3(0, 0, 0);

        float3 H = normalize(V + L);
            
        float NoH = saturate(dot(N, H));
        float NoL = saturate(dot(N, L));
        float NoV = saturate(dot(N, V));
        float LoH = saturate(dot(L, H));

        if (NoL <= 0.0 || NoV <= 0.0)
            return float3(0, 0, 0);

        float a = max(clearcoatRoughness, 0.045);
        float a2 = a * a;

        float denom = NoH * NoH * (a2 - 1.0) + 1.0;
        float D = a2 / (PI * denom * denom);

        float3 F0_cc = float3(0.04, 0.04, 0.04);
        float3 F = Fresnel(F0_cc, LoH);

        float Vis = V_Kelemen(LoH);

        float3 ccSpecular = D * F * Vis;

        float Fc = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);
        attenuation = 1.0 - clearcoatFactor * Fc;

        return clearcoatFactor * ccSpecular;
    }
        
}
    
namespace Anisotropic
{

    float D_GGX_Anisotropic(float NoH, float ToH, float BoH, float at, float ab)
    {
        float d = ToH * ToH / (at * at) + BoH * BoH / (ab * ab) + NoH * NoH;
        return 1.0 / (PI * at * ab * d * d);
    }

    float V_SmithGGX_Anisotropic(float NoV, float NoL, float ToV, float BoV, float ToL, float BoL, float at, float ab)
    {
        float lambdaV = NoL * length(float3(at * ToV, ab * BoV, NoV));
        float lambdaL = NoV * length(float3(at * ToL, ab * BoL, NoL));
        return 0.5 / max(lambdaV + lambdaL, 1e-7);
    }

    void ComputeAnisotropicRoughness(float roughness, float anisotropyFactor, out float at, out float ab)
    {
        float a = roughness * roughness;
        at = max(a * (1.0 + anisotropyFactor), 0.001);
        ab = max(a * (1.0 - anisotropyFactor), 0.001);
    }

    float3 Evaluate(
        in float roughness,
        in float anisotropyFactor,
        in float anisotropyRotation,
        in float3 N,
        in float3 V,
        in float3 L,
        in float3 T,
        in float3 B,
        in float3 F0)
    {
        if (anisotropyFactor <= 0.0)
            return Specular::GGX::F(roughness, N, V, L, F0);

        float cosR = cos(anisotropyRotation);
        float sinR = sin(anisotropyRotation);
            
        float3 T2 = T * cosR + B * sinR;
        float3 B2 = B * cosR - T * sinR;

        float3 H = normalize(V + L);

        float NoV = max(dot(N, V), 1e-4);
        float NoL = saturate(dot(N, L));
        float NoH = saturate(dot(N, H));
        float LoH = saturate(dot(L, H));

        if (NoL <= 0.0)
            return float3(0, 0, 0);

        float ToH = dot(T2, H);
        float BoH = dot(B2, H);
        float ToV = dot(T2, V);
        float BoV = dot(B2, V);
        float ToL = dot(T2, L);
        float BoL = dot(B2, L);

        float at, ab;
        ComputeAnisotropicRoughness(roughness, anisotropyFactor, at, ab);

        float  D   = D_GGX_Anisotropic(NoH, ToH, BoH, at, ab);
        float  Vis = V_SmithGGX_Anisotropic(NoV, NoL, ToV, BoV, ToL, BoL, at, ab);
        float3 F   = Fresnel(F0, LoH);

        return D * Vis * F;
    }
        
}
    
namespace Sheen
{

    float D_Charlie(float roughness, float NoH)
    {
        float a        = roughness * roughness;
        float invAlpha = 1.0 / a;
            
        float cos2h = NoH * NoH;
        float sin2h = max(1.0 - cos2h, 0.0078125);
        return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
    }

    float V_Ashikhmin(float NoV, float NoL)
    {
        return 1.0 / (4.0 * (NoL + NoV - NoL * NoV));
    }

    float3 Evaluate(
        in float3 sheenColor,
        in float sheenRoughness,
        in float3 N,
        in float3 V,
        in float3 L)
    {
        if (all(sheenColor <= float3(0, 0, 0)))
            return float3(0, 0, 0);

        float3 H = normalize(V + L);
            
        float NoH = saturate(dot(N, H));
        float NoV = max(dot(N, V), 1e-4);
        float NoL = saturate(dot(N, L));

        if (NoL <= 0.0)
            return float3(0, 0, 0);

        float D   = D_Charlie(max(sheenRoughness, 0.045), NoH);
        float Vis = V_Ashikhmin(NoV, NoL);

        return sheenColor * D * Vis;
    }

    float EnergyAttenuation(float3 sheenColor, float sheenRoughness, float NoV)
    {
        // Simplified approximation without LUT
        float fresnel  = pow(1.0 - NoV, 5.0);
        float maxSheen = max(sheenColor.r, max(sheenColor.g, sheenColor.b));
        return saturate(1.0 - maxSheen * (0.5 + 0.5 * fresnel) * (1.0 - sheenRoughness));
    }

}
    
namespace Subsurface
{

    float3 WrapDiffuse(
        in float3 albedo,
        in float subsurfaceFactor,
        in float3 N,
        in float3 L)
    {
        float NoL  = dot(N, L);
        float wrap = subsurfaceFactor;

        float wrapNoL = (NoL + wrap) / ((1.0 + wrap) * (1.0 + wrap));
              wrapNoL = max(wrapNoL, 0.0);

        return albedo * wrapNoL;
    }

    float3 Evaluate(
        in float3 albedo,
        in float subsurfaceFactor,
        in float3 N,
        in float3 V,
        in float3 L)
    {
        if (subsurfaceFactor <= 0.0)
            return float3(0, 0, 0);

        float3 H_back      = normalize(L + N * 0.6);
        float  VoH_back    = saturate(dot(V, -H_back));
        float  backScatter = pow(VoH_back, 3.0) * subsurfaceFactor;

        float3 wrapDiffuse = WrapDiffuse(albedo, subsurfaceFactor * 0.5, N, L);

        return wrapDiffuse + albedo * backScatter * 0.25;
    }

}
    
    
struct MaterialParams
{
    float3 albedo;
    float3 F0;
        
    float roughness;
    float metallic;

    float clearcoat;
    float clearcoatRoughness;
    float anisotropy;
    float anisotropyRotation;
        
    float3 sheenColor;
    float  sheenRoughness;
    float  subsurface;
    float  specularStrength;
};
    
float3 Evaluate(
    in MaterialParams mp,
    in float3 N,
    in float3 V,
    in float3 L,
    in float3 T,
    in float3 B)
{
    float NoL = saturate(dot(N, L));
    if (NoL <= 0.0 && mp.subsurface <= 0.0)
        return float3(0, 0, 0);
        
    // 式式式式式式式式式式式式式式式式 Base Diffuse 式式式式式式式式式式式式式式式式
    float3 diffuse = Diffuse::Hammon::F(mp.albedo, mp.roughness, N, V, L, mp.F0);
    
    float3 sssContrib = float3(0, 0, 0);
    if (mp.subsurface > 0.0)
    {
        diffuse    *= (1.0 - mp.subsurface);
        sssContrib = Subsurface::Evaluate(mp.albedo, mp.subsurface, N, V, L);
    }
        
    // 式式式式式式式式式式式式式式式式 Base Specular 式式式式式式式式式式式式式式式式
    float3 specular;
    if (mp.anisotropy > 0.0 && any(T))
    {
        specular = Anisotropic::Evaluate(
            mp.roughness, mp.anisotropy, mp.anisotropyRotation,
            N, V, L, T, B, mp.F0);
        }
    else
    {
        specular = Specular::GGX::F(mp.roughness, N, V, L, mp.F0);
    }
    specular *= mp.specularStrength;
    
    // 式式式式式式式式式式式式式式式式 Sheen Layer 式式式式式式式式式式式式式式式式
    float3 sheen            = float3(0, 0, 0);
    float  sheenAttenuation = 1.0;
    if (any(mp.sheenColor > float3(0, 0, 0)))
    {
        sheen            = Sheen::Evaluate(mp.sheenColor, mp.sheenRoughness, N, V, L);
        sheenAttenuation = Sheen::EnergyAttenuation(mp.sheenColor, mp.sheenRoughness, max(dot(N, V), 1e-4));
    }

    // 式式式式式式式式式式式式式式式式 Clearcoat Layer 式式式式式式式式式式式式式式式式
    float3 clearcoatSpec = float3(0, 0, 0);
    float  ccAttenuation = 1.0;
    if (mp.clearcoat > 0.0)
    {
        clearcoatSpec = Clearcoat::Evaluate(mp.clearcoat, mp.clearcoatRoughness, N, V, L, ccAttenuation);
    }

    // 式式式式式式式式式式式式式式式式 Compose 式式式式式式式式式式式式式式式式
    // Energy conservation: base is attenuated by clearcoat and sheen
    float3 H = normalize(V + L);
    float3 F = BxDF::Fresnel(mp.F0, saturate(dot(H, V)));
    float3 baseBRDF = (diffuse * (1.0 - F) + specular) * ccAttenuation * sheenAttenuation;

    return NoL * (baseBRDF + sheen) + sssContrib * ccAttenuation * sheenAttenuation + clearcoatSpec;
}
    
}


#endif // BXDF_HLSL