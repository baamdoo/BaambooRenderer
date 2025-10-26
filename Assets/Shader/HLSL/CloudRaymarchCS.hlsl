#define _CAMERA
#include "Common.hlsli"
#define _ATMOSPHERE
#include "AtmosphereCommon.hlsli"
#include "HelperFunctions.hlsli"
#include "Noise.hlsli"

struct CloudData
{
    float coverage;
    float cloudType;
    float precipitation;
    float padding0;

    float  topLayer_km;
    float  bottomLayer_km;
    float2 padding1;

    float baseNoiseScale;
    float baseIntensity;
    float detailNoiseScale;
    float detailIntensity;

    float3 windDirection;
    float  windSpeed_mps;
};
ConstantBuffer< CloudData > g_Cloud : register(b2);

// HLSL에서는 텍스처와 샘플러를 별도로 선언합니다.
Texture3D< float4 > g_CloudBaseNoise     : register(t3);
Texture3D< float4 > g_CloudDetailNoise   : register(t4);
Texture2D< float >  g_VerticalProfileLUT : register(t5);
Texture2D< float >  g_DepthBuffer        : register(t6);
Texture2D< float3 > g_TransmittanceLUT   : register(t7);
Texture2D< float3 > g_MultiScatteringLUT : register(t8);

RWTexture2D< float4 > g_CloudScatteringLUT : register(u0);

SamplerState g_LinearClampSampler : register(SAMPLER_INDEX_LINEAR_CLAMP);
SamplerState g_LinearWrapSampler  : register(SAMPLER_INDEX_LINEAR_WRAP);
SamplerState g_PointClampSampler  : register(SAMPLER_INDEX_POINT_CLAMP);

cbuffer PushConstants : register(b3, ROOT_CONSTANT_SPACE)
{
    float    time_s;
    uint64_t frame;
};

#ifndef PI
#define PI 3.1415926535f
#endif

static const float CLOUD_FORWARD_SCATTERING_G = 0.8;
static const float CLOUD_BACKWARD_SCATTERING_G = -0.2;
static const float CLOUD_FORWARD_SCATTERING_BLEND = 0.5;
static const float3 EXTINCTION_MULTIPLIER = float3(0.8, 0.8, 1.0);

static const uint CLOUD_RAYMARCH_STEPS = 128;
static const uint CLOUD_SHADOW_RAYMARCH_STEPS = 12;

float phase_HG(float cosTheta, float g)
{
    float g2 = g * g;
    float num = 1.0 - g2;
    float denom = 4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

float phase_Draine(float cosTheta, float g, float a)
{
    float g2 = g * g;
    float num = (1.0 - g2) * (1.0 + a * cosTheta * cosTheta);
    float denom = (1.0 + (a * (1.0 + 2.0 * g2)) / 3.0) * 4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

float PowderEffect(float depth, float height, float VoL)
{
    float r = -abs(VoL) * 0.5 + 0.5;
    r = r * r;
    height = height * (1.0 - r) + r;
    return depth * height;
}

float GetCloudBaseShape(float3 pos)
{
    float3 baseUVW   = pos * g_Cloud.baseNoiseScale;
    float4 baseNoise = g_CloudBaseNoise.Sample(g_LinearWrapSampler, baseUVW);

    float  baseCloud = saturate(remap(baseNoise.r, dot(baseNoise.gba, float3(0.625, 0.125, 0.25)) - 1.0, 1.0, 0.0, 1.0));
    return baseCloud;
}

float GetCloudDetailShape(float3 pos, float hNorm)
{
    float3 detailUVW   = pos * g_Cloud.detailNoiseScale;
    float3 detailNoise = g_CloudDetailNoise.Sample(g_LinearWrapSampler, detailUVW).rgb;

    float detailShape = dot(detailNoise, float3(0.625, 0.25, 0.125));
          detailShape = g_Cloud.detailIntensity * lerp(detailShape, 1.0 - detailShape, saturate(hNorm * 10.0));
    return detailShape;
}

float SampleCloudDensity(float3 pos, float hNorm)
{
    if (hNorm < 0.0)
    {
        return 0.0;
    }

    pos -= g_Cloud.windDirection * time_s * g_Cloud.windSpeed_mps;

    float verticalProfile    = g_VerticalProfileLUT.Sample(g_LinearClampSampler, float2(g_Cloud.cloudType, hNorm)).r;
    float dimensionalProfile = verticalProfile * g_Cloud.coverage;

    float f = GetCloudBaseShape(pos);
    f = saturate(f - (1.0 - dimensionalProfile));

    if (f > 0.0)
    {
        float d = GetCloudDetailShape(pos, hNorm);
        f = safeRemap(f, d, 1.0, 0.0, 1.0);
    }

    f *= g_Cloud.baseIntensity;
    return f;
}

float MultipleOctaveScattering(float density, float cosTheta)
{
    float attenuation = 0.59;
    float contribution = 0.84;
    float phaseAttenuation = 1.0;

    float a = attenuation;
    float b = contribution;
    float c = phaseAttenuation;
    const int scatteringOctaves = 8;

    float luminance = 0.0;
    for (int i = 0; i < scatteringOctaves; ++i)
    {
        float forwardPhase = phase_HG(cosTheta, c * 0.9881);
        float drainePhase = phase_Draine(cosTheta, 0.5567, 21.9955);
        float phase = lerp(forwardPhase, drainePhase, 0.4824);
        float beers = exp(-density * 0.424 * a);
        luminance += b * phase * beers;
        a *= attenuation;
        b *= contribution;
    }
    return luminance;
}

float3 RaymarchLight(float3 rayOrigin, float3 rayDirection, float VoL)
{
    float rTopLayer = g_Atmosphere.planetRadius_km + g_Cloud.topLayer_km;
    float2 topIntersection = RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rTopLayer);
    if (all(topIntersection < float2(0.0, 0.0)))
    {
        return float3(1.0, 1.0, 1.0);
    }

    float shadowMarchLength = topIntersection.y;
    float invShadowStepCount = 1.0 / (float)CLOUD_SHADOW_RAYMARCH_STEPS;

    float tPrev = 0.0;
    float density = 0.0;
    for (float st = invShadowStepCount; st <= 1.0 + 0.001; st += invShadowStepCount)
    {
        float tCurr = st * st;
        float tDelta = tCurr - tPrev;
        float tShadow = shadowMarchLength * (tCurr - 0.5 * tDelta);
        float3 spos = rayOrigin + tShadow * rayDirection;
        float saltitude = length(spos) - g_Atmosphere.planetRadius_km;
        float shNorm = inverseLerp(saltitude, g_Cloud.bottomLayer_km, g_Cloud.topLayer_km);
        if (shNorm > 1.0)
        {
            break;
        }
        density += max(0, SampleCloudDensity(spos, shNorm) * shadowMarchLength * tDelta);
        tPrev = tCurr;
    }

    float beers = exp(-density);
    return float3(beers, beers, beers);
}

float4 RaymarchCloud(float3 rayOrigin, float3 rayDirection, float maxDistance)
{
    float rBottomLayer = g_Atmosphere.planetRadius_km + g_Cloud.bottomLayer_km;
    float rTopLayer = g_Atmosphere.planetRadius_km + g_Cloud.topLayer_km;

    float2 bottomIntersection = RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rBottomLayer);
    float2 topIntersection = RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rTopLayer);

    float rayStart, rayEnd;
    if (length(rayOrigin) >= rBottomLayer && length(rayOrigin) <= rTopLayer)
    {
        rayStart = 0.0;
        rayEnd = topIntersection.y > 0.0 ? min(topIntersection.y, maxDistance) : maxDistance;
    }
    else if (bottomIntersection.y > 0.0)
    {
        rayStart = min(bottomIntersection.y, maxDistance);
        rayEnd = min(topIntersection.y, maxDistance);
    }
    else if (topIntersection.x > 0.0 && topIntersection.x < maxDistance)
    {
        rayStart = topIntersection.x;
        rayEnd = min(bottomIntersection.y, maxDistance);
    }
    else
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float rayLength = rayEnd - rayStart;
    if (rayLength <= 0.0)
        return float4(0.0, 0.0, 0.0, 1.0);

    float3 sunDirection = normalize(float3(-g_Atmosphere.light.dirX, -g_Atmosphere.light.dirY, -g_Atmosphere.light.dirZ));
    float3 lightColor = float3(g_Atmosphere.light.colorR, g_Atmosphere.light.colorG, g_Atmosphere.light.colorB);
    if (g_Atmosphere.light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(g_Atmosphere.light.temperature_K);
    float3 E = g_Atmosphere.light.illuminance_lux * lightColor;

    float VoL = dot(rayDirection, sunDirection);
    float forwardPhase = phase_HG(VoL, 0.8);
    float phase = forwardPhase;

    float stepSize = rayLength / (float)CLOUD_RAYMARCH_STEPS;
    float tSample = rayStart - stepSize * hash1D(rayDirection);

    float3 L = float3(0.0, 0.0, 0.0);
    float throughput = 1.0;
    for (int i = 0; i < CLOUD_RAYMARCH_STEPS; i++)
    {
        float3 pos = rayOrigin + tSample * rayDirection;
        float sampleHeight = length(pos);
        float sampleTheta = dot(normalize(pos), sunDirection);
        float3 transmittanceToLight = SampleTransmittanceLUT(g_TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km);

        float altitude = sampleHeight - g_Atmosphere.planetRadius_km;
        float hNorm = inverseLerp(altitude, g_Cloud.bottomLayer_km, g_Cloud.topLayer_km);

        float stepDensity = SampleCloudDensity(pos, hNorm);
        if (stepDensity > EPSILON)
        {
            float opticalDepth = stepDensity * stepSize;
            float stepTransmittance = exp(-opticalDepth);
            float3 extinction = RaymarchLight(pos, sunDirection, VoL);
            float d = pow(clamp(stepDensity * 8.0, 0.0, 1.0), safeRemap(hNorm, 0.3, 0.85, 0.5, 2.0)) + 0.05;
            float v = pow(safeRemap(hNorm, 0.07, 0.22, 0.1, 1.0), 0.8);
            float powder = PowderEffect(d, v, sampleTheta);
            float2 msUV = clamp(
                float2(sampleTheta * 0.5 + 0.5, inverseLerp(sampleHeight, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km)),
                0.0, 1.0);
            float3 multiScattering = g_MultiScatteringLUT.Sample(g_LinearClampSampler, msUV).rgb;
            float3 ambientLit = multiScattering * pow(1.0 - hNorm, 0.5);
            float3 S = (ambientLit + E * transmittanceToLight * extinction) * stepDensity;
            float3 Sint = (S - S * stepTransmittance) / max(stepDensity, EPSILON);
            L += throughput * Sint;
            throughput *= stepTransmittance;
        }

        if (throughput <= 1e-3)
        {
            break;
        }
        tSample += stepSize;
    }

    return float4(L, throughput);
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    g_CloudScatteringLUT.GetDimensions(width, height);
    int2 imgSize = int2(width, height);

    int2 pixCoords = (int2)dispatchThreadID.xy;
    if (any(pixCoords >= imgSize))
        return;

    float2 uv = (float2(pixCoords) + 0.5) / (float2)imgSize;
    float depth = g_DepthBuffer.Sample(g_PointClampSampler, uv).r;

    float3 cameraPos = float3(g_Camera.posWORLD.x, max(g_Camera.posWORLD.y, MIN_VIEW_HEIGHT_ABOVE_GROUND), g_Camera.posWORLD.z);
    float3 cameraPosAbovePlanet = cameraPos * DISTANCE_SCALE + float3(0.0, g_Atmosphere.planetRadius_km, 0.0);

    float3 posWORLD = ReconstructWorldPos(uv, depth, g_Camera.mViewProjInv);
    float3 rayDirection = normalize(posWORLD - g_Camera.posWORLD);
    float3 rayOrigin = cameraPosAbovePlanet;

    float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, g_Atmosphere.planetRadius_km);
    float maxDistance = groundIntersection.x > 0.0 ? groundIntersection.x : groundIntersection.y > 0.0 ? groundIntersection.y : RAY_MARCHING_MAX_DISTANCE;
    maxDistance = min(maxDistance, depth == 1.0 ? RAY_MARCHING_MAX_DISTANCE : length(posWORLD - g_Camera.posWORLD) * DISTANCE_SCALE);

    float4 clouds = RaymarchCloud(rayOrigin, rayDirection, maxDistance);
    g_CloudScatteringLUT[pixCoords] = clouds;
}