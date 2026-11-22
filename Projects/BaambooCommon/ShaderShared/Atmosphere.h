#pragma once
#include "Light.h"
#include "Cloud.h"

struct AtmosphereData
{
    DirectionalLight light;

    float  planetRadiusKm;
    float  atmosphereRadiusKm;
    float2 padding0;

    float3 rayleighScattering;
    float  rayleighDensityKm;

    float mieScattering;
    float mieAbsorption;
    float mieDensityKm;
    float miePhaseG;

    float3 ozoneAbsorption;
    float  ozoneCenterKm;

    float3 groundAlbedo;
    float  ozoneWidthKm;
};

struct SceneEnvironmentData
{
    AtmosphereData atmosphere;
    CloudData      cloud;
};

struct AtmosphereRaymarchConstants
{
    u32 msIsoSampleCount; // also the distant-sky-light hemisphere sample count
    u32 msNumRaySteps;
    u32 svMinRaySteps;
    u32 svMaxRaySteps;
};

#ifdef __cplusplus
static_assert(sizeof(AtmosphereRaymarchConstants) == 16);
#endif
