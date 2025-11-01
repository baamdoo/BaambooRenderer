#define _CAMERA
#include "Common.hlsli"
#define _ATMOSPHERE
#include "AtmosphereCommon.hlsli"
#define _CLOUD
#include "CloudCommon.hlsli"

Texture2D< float >  g_DepthBuffer          : register(t4);
Texture2D< float3 > g_TransmittanceLUT     : register(t5);
Texture3D< float4 > g_AerialPerspectiveLUT : register(t6);
Texture1D< float3 > g_AtmosphereAmbientLUT : register(t7);
Texture2DArray< float2 > g_BlueNoiseArray  : register(t8);

RWTexture2D< float4 > g_CloudScatteringLUT : register(u0);

cbuffer PushConstant : register(b0, ROOT_CONSTANT_SPACE)
{
    uint NumCloudRaymarchSteps;
    uint NumLightRaymarchSteps;

    float    TimeSec;
    uint64_t Frame;
};

static const int MS_OCTAVES = 2;
static const int SCATTERING_OCTAVES = 1 + MS_OCTAVES;

////////////////////////////////////////////////////////////////////////////
// Phase //
// Reference: https://research.nvidia.com/labs/rtr/approximate-mie/
struct ParticipatingMediaPhaseContext
{
    float phase0[SCATTERING_OCTAVES];
    // TODO. float phase1[SCATTERING_OCTAVES];
};

ParticipatingMediaPhaseContext SetupParticipatingMediaPhaseContext(float basePhase0, float baseMsPhaseStrength)
{
    ParticipatingMediaPhaseContext PMPC;
    PMPC.phase0[0] = basePhase0;
    // TODO. PMPC.phase1[0] = basePhase1;

    float isoPhase        = IsotropicPhase();
    float msPhaseStrength = baseMsPhaseStrength;

    // becomes isotropic-phase as the scattering level increases
    for (int ms = 1; ms < SCATTERING_OCTAVES; ++ms)
    {
        PMPC.phase0[ms] = lerp(isoPhase, PMPC.phase0[0], msPhaseStrength);
        // TODO. PMPC.phase1[ms] = lerp(isoPhase, PMPC.phase1[0], msPhaseStrength);
        msPhaseStrength *= msPhaseStrength;
    }

    return PMPC;
}

float phase_HG(float cosTheta, float g)
{
    float g2    = g * g;
    float num   = 1.0 - g2;
    float denom = 4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);

    return num / denom;
}

float phase_Draine(float cosTheta, float g, float a)
{
    float g2    = g * g;
    float num   = (1.0 - g2) * (1.0 + a * cosTheta * cosTheta);
    float denom = (1.0 + (a * (1.0 + 2.0 * g2)) / 3.0) * 4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);

    return num / denom;
}

float phase_DualLob(float cosTheta, float g0, float g1, float w)
{
    return lerp(phase_HG(cosTheta, g0), phase_HG(cosTheta, g1), w);
}

float phase_DraineHG(float cosTheta, float gHG, float gD, float a, float w)
{
    return lerp(phase_HG(cosTheta, gHG), phase_Draine(cosTheta, gD, a), w);
}

//////////////////////////////////////////////////////////////////////////////
// Shading //
// Reference: https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Shaders/Private/VolumetricCloud.usf
//            https://blog.selfshadow.com/publications/s2020-shading-course/hillaire/s2020_pbs_hillaire_slides.pdf
struct ParticipatingMediaTransmittanceContext
{
    float3 transmittanceToLight0[SCATTERING_OCTAVES];
    // TODO. float3 transmittanceToLight1[SCATTERING_OCTAVES];
};

struct ParticipatingMediaExtinctionContext
{
    float3 cScattering[SCATTERING_OCTAVES];
    float3 cExtinction[SCATTERING_OCTAVES];
};

ParticipatingMediaExtinctionContext SetupParticipatingMediaExtinctionContext(float3 baseAlbedo, float3 baseExtinction, float msScattering, float msExtinction)
{
    const float3 cScattering = baseAlbedo * baseExtinction;

    ParticipatingMediaExtinctionContext PMEC;
    PMEC.cScattering[0] = cScattering;
    PMEC.cExtinction[0] = baseExtinction;

    for (int ms = 1; ms < SCATTERING_OCTAVES; ++ms)
    {
        PMEC.cScattering[ms] = PMEC.cScattering[ms - 1] * msScattering;
        PMEC.cExtinction[ms] = PMEC.cExtinction[ms - 1] * msExtinction;
        msScattering *= msScattering;
        msExtinction *= msExtinction;
    }

    return PMEC;
}

// https://www.guerrilla-games.com/read/nubis-authoring-real-time-volumetric-cloudscapes-with-the-decima-engine
float InscatterProbability(float density, float hNorm, float cosTheta)
{
    float d = pow(saturate(density * 8.0), safeRemap(hNorm, 0.3, 0.85, 0.5, 2.0)) + 0.05;
    float v = pow(safeRemap(hNorm, 0.07, 0.14, 0.1, 1.0), 0.8);
    float inscatter = d * v;

    return inscatter;
}

ParticipatingMediaTransmittanceContext RaymarchLight(float3 rayOrigin, float3 rayDirection, float VoL, float msExtinctionStrength, float jitter)
{
    ParticipatingMediaTransmittanceContext PMTC;

    int ms = 0;
    float  stepDensity[SCATTERING_OCTAVES];
    float3 opticalDepth[SCATTERING_OCTAVES];

    for (ms = 0; ms < SCATTERING_OCTAVES; ++ms)
    {
        stepDensity[ms]  = 0.0;
        opticalDepth[ms] = float3(0.0, 0.0, 0.0);
    }

    float rTopLayer = g_Atmosphere.planetRadius_km + g_Cloud.topLayer_km;
    float2 topIntersection =
        RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rTopLayer);

    if (all(topIntersection < float2(0.0, 0.0)))
    {
        return PMTC;
    }

    float3 ExtinctionStrength = g_Cloud.extinctionStrength * g_Cloud.extinctionScale;

    float shadowMarchLength  = topIntersection.y;
    float invShadowStepCount = 1.0 / (float)NumLightRaymarchSteps;
    float startOffset        = jitter * invShadowStepCount;

    float tPrev = 0.0;
    for (float st = invShadowStepCount; st <= 1.0 + 0.001; st += invShadowStepCount)
    {
        float tCurr   = st * st;  // non-linear shadow sample distribution
        float tDelta  = tCurr - tPrev;
        float tShadow = shadowMarchLength * (tCurr - 0.5 * tDelta);

        float3 spos = rayOrigin + tShadow * rayDirection;

        float saltitude = length(spos) - g_Atmosphere.planetRadius_km;
        float shNorm    = inverseLerp(saltitude, g_Cloud.bottomLayer_km, g_Cloud.topLayer_km);
        if (shNorm > 1.0)
        {
            break;
        }

        float stepLength = shadowMarchLength * tDelta;

        float3 offset = g_Cloud.windDirection * TimeSec * g_Cloud.windSpeed_mps * 0.001;

        stepDensity[0]  = SampleCloudDensity(spos, shNorm, offset);
        opticalDepth[0] += stepDensity[0] * stepLength * ExtinctionStrength * 1000.0;

        float MsExtinctionStrength = msExtinctionStrength;
        for (ms = 1; ms < SCATTERING_OCTAVES; ++ms)
        {
            stepDensity[ms] = stepDensity[ms - 1] * MsExtinctionStrength;
            opticalDepth[ms] += stepDensity[ms] * stepLength * ExtinctionStrength * 1000.0;

            MsExtinctionStrength *= MsExtinctionStrength;
        }

        tPrev = tCurr;
    }

    for (ms = 0; ms < SCATTERING_OCTAVES; ++ms)
    {
        // https://www.guerrilla-games.com/read/nubis-authoring-real-time-volumetric-cloudscapes-with-the-decima-engine
        PMTC.transmittanceToLight0[ms] = max(exp(-opticalDepth[ms]), exp(-opticalDepth[ms] * 0.25) * 0.7);
    }

    return PMTC;
}

struct CloudResult
{
    float3 L;          // Luminance
    float3 throughput; // Transmittance
    float  apDistance; // Weighted Average Distance for AP
};

CloudResult RaymarchCloud(float3 rayOrigin, float3 rayDirection, float maxDistance, float2 jitter)
{
    CloudResult result;
    result.L          = float3(0.0, 0.0, 0.0);
    result.throughput = float3(1.0, 1.0, 1.0);
    result.apDistance = 0.0;

    float rBottomLayer = g_Atmosphere.planetRadius_km + g_Cloud.bottomLayer_km;
    float rTopLayer    = g_Atmosphere.planetRadius_km + g_Cloud.topLayer_km;

    float2 bottomIntersection =
        RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rBottomLayer);
    float2 topIntersection =
        RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rTopLayer);

    float rayStart = bottomIntersection.y > 0.0 ? bottomIntersection.y : 0.0;
    float rayEnd   = topIntersection.y;

    if (length(rayOrigin) >= rBottomLayer && length(rayOrigin) <= rTopLayer)
    {
        // camera in-between cloud layers
        rayStart = 0.0;
        rayEnd   = topIntersection.y > 0.0 ? min(topIntersection.y, maxDistance) : maxDistance;
    }
    else if (bottomIntersection.y > 0.0)
    {
        // camera in-below cloud bottom layer
        rayStart = min(bottomIntersection.y, maxDistance);
        rayEnd   = min(topIntersection.y, maxDistance);
    }
    else if (topIntersection.x > 0.0 && topIntersection.x < maxDistance)
    {
        // camera in-above cloud top layer
        rayStart = topIntersection.x;
        rayEnd   = min(bottomIntersection.y, maxDistance);
    }
    else
    {
        // no cloud intersection
        return result;
    }

    float rayLength = rayEnd - rayStart;
    if (rayLength <= 0.0)
        return result;

    float3 sunDirection = normalize(float3(-g_Atmosphere.light.dirX, -g_Atmosphere.light.dirY, -g_Atmosphere.light.dirZ));
    float3 lightColor   = float3(g_Atmosphere.light.colorR, g_Atmosphere.light.colorG, g_Atmosphere.light.colorB);
    if (g_Atmosphere.light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(g_Atmosphere.light.temperature_K);

    float3 E = g_Atmosphere.light.illuminance_lux * lightColor;

    float VoL = dot(rayDirection, sunDirection);

    // --- Phase function --- //
    //float phase = phase_DualLob(VoL, CLOUD_FORWARD_SCATTERING_G, CLOUD_BACKWARD_SCATTERING_G, CLOUD_PHASE_BLEND_ALPHA);
    float baseScatter   = phase_DraineHG(VoL, 0.9881, 0.5567, 21.9955, 0.4824);
    float silverScatter = 0.5 * phase_HG(VoL, 0.99);

    float phase = max(baseScatter, silverScatter);

    ParticipatingMediaPhaseContext PMPC = SetupParticipatingMediaPhaseContext(phase, g_Cloud.msEccentricity);
    // ---------------------- //

    // --- Sampling setup --- //
    float3 ExtinctionFactor = g_Cloud.extinctionStrength * g_Cloud.extinctionScale;

    float stepSize = rayLength / (float)NumCloudRaymarchSteps;
    float tSample  = rayStart + stepSize * jitter.r;
    // ---------------------- //

    float apScale       = 0.0;
    float apDistanceAcc = 0.0;

    float3 L          = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);
    for (int i = 0; i < (float)NumCloudRaymarchSteps; ++i)
    {
        float3 samplePos = rayOrigin + tSample * rayDirection;

        float sampleTheta              = dot(normalize(samplePos), sunDirection);
        float sampleHeight             = length(samplePos);
        float3 atmosphereTransmittance = SampleTransmittanceLUT(g_TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km);

        float altitude = sampleHeight - g_Atmosphere.planetRadius_km;
        float hNorm    = inverseLerp(altitude, g_Cloud.bottomLayer_km, g_Cloud.topLayer_km);

        float3 offset     = g_Cloud.windDirection * TimeSec * g_Cloud.windSpeed_mps * 0.001;
        float stepDensity = SampleCloudDensity(samplePos, hNorm, offset);
        if (stepDensity > 0.0f)
        {
            // --- Prepare AerialPerspective --- //
            float apWeight = min(throughput.r, min(throughput.g, throughput.b));
            apDistanceAcc += tSample * apWeight;
            apScale       += apWeight;
            // --------------------------------- //

            float powder = InscatterProbability(stepDensity, hNorm, VoL);

            float3 stepExtinction = stepDensity * ExtinctionFactor;
            float3 albedo         = pow(saturate(stepExtinction * 1000.0), float3(0.25, 0.25, 0.25)) * powder * 10.0;

            float3 opticalDepth      = max(stepExtinction, 1e-8) * stepSize * 1000.0;
            float3 stepTransmittance = exp(-opticalDepth);

            float msScatteringStrength = g_Cloud.msContribution;
            float msExtinctionStrength = g_Cloud.msOcclusion;

            ParticipatingMediaExtinctionContext    PMEC = SetupParticipatingMediaExtinctionContext(albedo, stepExtinction, msScatteringStrength, msExtinctionStrength);
            ParticipatingMediaTransmittanceContext PMTC = RaymarchLight(samplePos, sunDirection, VoL, msExtinctionStrength, jitter.g);

            float3 ambientLit = g_AtmosphereAmbientLUT.Sample(g_LinearClampSampler, hNorm).rgb;
            // --- Ground Contribution --- //
            if (g_Cloud.groundContributionStrength > 0.0)
            {
                float3 Nground = normalize(samplePos - PLANET_CENTER);
                float3 Dground = -Nground;

                const float groundRayLength = altitude;
                const float groundStepCount = 5.0;
                const float invGroundStepCount = 1.0 / groundStepCount;

                float tPrev = 0.0;
                float3 ODground = float3(0.0, 0.0, 0.0);
                for (float tGround = invGroundStepCount; tGround <= 1.00001; tGround += invGroundStepCount)
                {
                    float tCurr  = tGround * tGround;
                    float tDelta = tCurr - tPrev;

                    float tSampleGround    = groundRayLength * (tCurr - 0.5 * tDelta);
                    float3 samplePosGround = samplePos + Dground * tSampleGround;

                    float altitudeGround = length(samplePosGround) - g_Atmosphere.planetRadius_km;
                    float hNormGround    = inverseLerp(altitudeGround, g_Cloud.bottomLayer_km, g_Cloud.topLayer_km);

                    float densityGround = SampleCloudDensity(samplePosGround, hNormGround, offset);

                    if (densityGround > 0.0)
                    {
                        float stepLengthKm = groundRayLength * tDelta;

                        ODground += densityGround * ExtinctionFactor * stepLengthKm * 1000.0;
                    }
                    tPrev = tCurr;
                }

                float3 GoL = saturate(dot(sunDirection, Nground)) * (g_Atmosphere.groundAlbedo.rgb / PI);
                float3 groundToCloudTransfer = (2.0f * PI) * IsotropicPhase() * GoL;

                float3 L0ground = E * atmosphereTransmittance * groundToCloudTransfer;

                ambientLit += L0ground * exp(-ODground) * g_Cloud.groundContributionStrength;
            }
            // -------------------------- //

            for (int ms = SCATTERING_OCTAVES - 1; ms >= 0; --ms)
            {
                float3 skyAmbient = ms == 0 ? ambientLit : float3(0.0, 0.0, 0.0);

                float3 S0    = (skyAmbient + E * atmosphereTransmittance * PMTC.transmittanceToLight0[ms] * PMPC.phase0[ms]) * PMEC.cScattering[ms];
                float3 S0int = (S0 - S0 * stepTransmittance) / max(PMEC.cExtinction[ms], 1e-8);

                L += throughput * S0int;
                if (ms == 0)
                {
                    throughput *= stepTransmittance;
                }
            }
        }

        if (all(float3(EPSILON, EPSILON, EPSILON) > throughput))
        {
            break;
        }

        tSample += stepSize;
    }

    result.L          = L;
    result.throughput = throughput;
    if (apScale > 0.0)
    {
        result.apDistance = apDistanceAcc / apScale;
    }

    return result;
}

[numthreads(8, 8, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    uint2 imgSize;
    uint2 pixCoords = tID.xy;
    g_CloudScatteringLUT.GetDimensions(imgSize.x, imgSize.y);
    if (tID.x >= imgSize.x || tID.y >= imgSize.y)
        return;

    float2 uv    = float2(tID.xy + 0.5) / float2(imgSize);
    float  depth = g_DepthBuffer.Sample(g_PointClampSampler, uv).r;

    float3 cameraPos =
        float3(g_Camera.posWORLD.x, max(g_Camera.posWORLD.y, MIN_VIEW_HEIGHT_ABOVE_GROUND), g_Camera.posWORLD.z);
    float3 cameraPosAbovePlanet =
        cameraPos * DISTANCE_SCALE + float3(0.0, g_Atmosphere.planetRadius_km, 0.0);

    float3 posWORLD     = ReconstructWorldPos(uv, depth, g_Camera.mViewProjInv);
    float3 rayDirection = normalize(posWORLD - g_Camera.posWORLD);
    float3 rayOrigin    = cameraPosAbovePlanet;

    float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, g_Atmosphere.planetRadius_km);
    float maxDistance         = groundIntersection.x > 0.0 ? groundIntersection.x : groundIntersection.y > 0.0 ? groundIntersection.y : RAY_MARCHING_MAX_DISTANCE;
          maxDistance         = min(maxDistance, depth == 1.0 ? RAY_MARCHING_MAX_DISTANCE : length(posWORLD - g_Camera.posWORLD) * DISTANCE_SCALE);

    float2 jitter;
    {
        uint3 noiseSize;
        g_BlueNoiseArray.GetDimensions(noiseSize.x, noiseSize.y, noiseSize.z);

        int2 offset     = int2(float2(0.754877669, 0.569840296) * (float)Frame * float2(imgSize));
        int2 noiseIdx   = (pixCoords.xy + offset) % noiseSize.xy;
        int  noiseSlice = (int)Frame % noiseSize.z;

        jitter = g_BlueNoiseArray.Load(int4(noiseIdx, noiseSlice, 0));
    }

    CloudResult cloud = RaymarchCloud(rayOrigin, rayDirection, maxDistance, jitter);
    float3 L = cloud.L;
    float  A = dot(cloud.throughput, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
    if (cloud.apDistance > 0.0)
    {
        // Aerial perspective
        {
            float viewDistanceKm = cloud.apDistance;

            uint3 apSize;
            g_AerialPerspectiveLUT.GetDimensions(apSize.x, apSize.y, apSize.z);

            float slice = viewDistanceKm * (1.0 / AP_KM_PER_SLICE);
            float w     = sqrt(slice / (float)apSize.z);
                  slice = w * apSize.z;

            float4 ap = g_AerialPerspectiveLUT.Sample(g_LinearClampSampler, float3(uv, w));

            // prevents an abrupt appearance of fog on objects close to the camera
            float weight = 1.0;
            if (slice < sqrt(0.5))
            {
                weight = clamp((slice * slice * 2.0), 0.0, 1.0);
            }
            ap.rgb *= weight;
            ap.a = 1.0 - weight * (1.0 - ap.a);

            // FinalColor = (SurfaceColor * Transmittance) + InScatteredLight
            L = L * ap.a + ap.rgb * (1.0 - A); // Apply (1.0 - A) to prevent neglectable noise values from meeting with AP and being amplified
        }
    }

    g_CloudScatteringLUT[pixCoords] = float4(L, A);
}