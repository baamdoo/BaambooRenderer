#ifndef _HLSL_ATMOSPHERE_COMMON_HEADER
#define _HLSL_ATMOSPHERE_COMMON_HEADER

#define _HLSL
#include "../Common.bsh"
#include "HelperFunctions.hlsli"

static const float  DISTANCE_SCALE               = 0.00001;       // cm-km
static const float3 PLANET_CENTER                = 0.0;           // km
static const float  AP_KM_PER_SLICE              = 4.0;           // km
static const float  MIN_VIEW_HEIGHT_ABOVE_GROUND = 0.0005 * (1.0 / DISTANCE_SCALE); // km

#ifdef _ATMOSPHERE
struct AtmosphereData
{
    DirectionalLight light;

    float  planetRadius_km;
    float  atmosphereRadius_km;
    float2 padding0;

    float3 rayleighScattering;
    float  rayleighDensityH_km;
    
    float mieScattering;
    float mieAbsorption;
    float mieDensityH_km;
    float miePhaseG;
    
    float3 ozoneAbsorption;
    float  ozoneCenter_km;
    float  ozoneWidth_km;

    float3 groundAlbedo;
}; ConstantBuffer< AtmosphereData > g_Atmosphere : register(b1);

// Reference: https://github.com/sebh/UnrealEngineSkyAtmosphere
float GetAltitude(float3 position) 
{
    return length(position) - g_Atmosphere.planetRadius_km;
}

float RayleighPhase(float cosTheta) 
{
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

// Cornette-Shanks
float MiePhase(float cosTheta, float g) 
{
    float g2    = g * g;
    float num   = 3.0 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float denom = 8.0 * PI * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

float GetDensityAtHeight(float altitude, float H) 
{
    return exp(-altitude / H);
}

float GetDensityOzoneAtHeight(float altitude) 
{
    float relativeAlt = abs(altitude - g_Atmosphere.ozoneCenter_km) / (g_Atmosphere.ozoneWidth_km * 0.5);
    return max(0.0, 1.0 - relativeAlt);
}

float3 SampleTransmittanceLUT(Texture2D< float3 > transmittanceLUT, SamplerState sampler, float height, float cosTheta, float bottomRadius, float topRadius) 
{
    float H            = safeSqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    float rho          = safeSqrt(height * height - bottomRadius * bottomRadius);
    float discriminant = height * height * (cosTheta * cosTheta - 1.0) + topRadius * topRadius;

    float d     = max(0.0, (-height * cosTheta + safeSqrt(discriminant)));
    float d_min = topRadius - height;
    float d_max = rho + H;
    
    float u = inverseLerp(d, d_min, d_max);
    float v = rho / H;
    
    return transmittanceLUT.SampleLevel(sampler, float2(u, v), 0).rgb;
}
#endif // _ATMOSPHERE

#endif // _HLSL_ATMOSPHERE_COMMON_HEADER