#define _CAMERA
#define _SCENEENVIRONMENT
#include "AtmosphereCommon.hlsli"
#include "CloudCommon.hlsli"

cbuffer PushConstant : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_NumCloudRaymarchSteps;

    float    g_TimeSec;
    uint64_t g_Frame;
};

ConstantBuffer< DescriptorHeapIndex > g_DepthBuffer           : register(b4, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_TransmittanceLUT      : register(b5, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_AerialPerspectiveLUT  : register(b6, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_AtmosphereAmbientLUT  : register(b7, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_BlueNoiseLUT          : register(b8, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutCloudScatteringLUT : register(b9, ROOT_CONSTANT_SPACE);

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

float3 CalculateCloudAmbient(float3 baseAmbient, float hNorm, float alpha)
{
    CloudData Cloud = GetCloudData();

    const float3 deepBlueTint  = float3(0.3, 0.4, 0.65);
    const float3 muddyGreyTint = float3(0.25, 0.20, 0.20);

    float topIntensity    = Cloud.topAmbientScale;
    float bottomIntensity = lerp(0.25, 0.75, hNorm) * Cloud.bottomAmbientScale;

    float3 ambientOvercasted  = lerp(baseAmbient, baseAmbient * muddyGreyTint, Cloud.localOvercast);
    float3 ambientDesaturated = Desaturate(ambientOvercasted, 1.0 - Cloud.ambientSaturation);

    float3 ambientLit = ambientDesaturated * Cloud.ambientIntensity * lerp(topIntensity, bottomIntensity, alpha);
    return ambientLit;
}

// [Deprecated]
// https://www.guerrilla-games.com/read/nubis-authoring-real-time-volumetric-cloudscapes-with-the-decima-engine
// float InscatterProbability(float density, float hNorm, float cosTheta)
// {
//     float d = pow(saturate(density * 8.0), safeRemap(hNorm, 0.3, 0.85, 0.5, 2.0)) + 0.05;
//     float v = pow(safeRemap(hNorm, 0.07, 0.14, 0.1, 1.0), 0.8);
//     float inscatter = d * v;
// 
//     return inscatter;
// }

ParticipatingMediaTransmittanceContext RaymarchLight(float3 rayOrigin, float3 rayDirection, float VoL, float msExtinctionStrength, float jitter)
{
    CloudData      Cloud      = GetCloudData();
    AtmosphereData Atmosphere = GetAtmosphereData();

    const float topLayerMeter     = Cloud.topLayerKm * KM_TO_M;
    const float bottomLayerMeter  = Cloud.bottomLayerKm * KM_TO_M;
    const float planetRadiusMeter = Atmosphere.planetRadiusKm * KM_TO_M;

    ParticipatingMediaTransmittanceContext PMTC;

    int ms = 0;
    float  extinction[SCATTERING_OCTAVES];
    float3 opticalDepth[SCATTERING_OCTAVES];

    for (ms = 0; ms < SCATTERING_OCTAVES; ++ms)
    {
        extinction[ms]   = 0.0;
        opticalDepth[ms] = 0.0;
    }

    float invShadowStepCount     = 1.0 / 16.0;
    float shadowMarchLengthMeter = Cloud.shadowTracingDistanceKm * 1000.0f;

    float3 movement = Cloud.windDirection * g_TimeSec * Cloud.windSpeedMps;

    float tPrev = 0.0;
    for (float st = invShadowStepCount; st <= 1.0 + 0.001; st += invShadowStepCount)
    {
        float tCurr   = st * st;  // non-linear shadow sample distribution
        float tDelta  = tCurr - tPrev;
        float tShadow = shadowMarchLengthMeter * (tCurr - 0.5 * tDelta);

        float3 spos = rayOrigin + tShadow * rayDirection;

        float saltitude = length(spos) - planetRadiusMeter;
        float shNorm    = inverseLerp(saltitude, bottomLayerMeter, topLayerMeter);
        if (shNorm > 1.0)
        {
            break;
        }

        float3 sposInLayer         = float3(spos.x, saltitude, spos.z);
        float3 conservativeDensity = SampleCloudConservativeDensity(sposInLayer, shNorm, Cloud);
        if (conservativeDensity.x <= 0.0)
        {
            continue;
        }

        float distToCamera = length(sposInLayer - g_Camera.posWORLD);

        extinction[0]   = SampleCloudExtinction(sposInLayer, shNorm, distToCamera, conservativeDensity, false, Cloud).extinction;
        opticalDepth[0] += extinction[0] * tDelta;

        float MsExtinctionStrength = msExtinctionStrength;
        for (ms = 1; ms < SCATTERING_OCTAVES; ++ms)
        {
            extinction[ms]   = extinction[ms - 1] * MsExtinctionStrength;
            opticalDepth[ms] += extinction[ms] * tDelta;

            MsExtinctionStrength *= MsExtinctionStrength;
        }

        tPrev = tCurr;
    }

    for (ms = 0; ms < SCATTERING_OCTAVES; ++ms)
    {
        PMTC.transmittanceToLight0[ms] = exp(-opticalDepth[ms] * shadowMarchLengthMeter);
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
    CloudData      Cloud      = GetCloudData();
    AtmosphereData Atmosphere = GetAtmosphereData();

    const float topLayerMeter         = Cloud.topLayerKm * KM_TO_M;
    const float bottomLayerMeter      = Cloud.bottomLayerKm * KM_TO_M;
    const float planetRadiusMeter     = Atmosphere.planetRadiusKm * KM_TO_M;
    const float atmosphereRadiusMeter = Atmosphere.atmosphereRadiusKm * KM_TO_M;

    Texture2D< float3 > TransmittanceLUT     = GetResource(g_TransmittanceLUT.index);
    Texture1D< float3 > AtmosphereAmbientLUT = GetResource(g_AtmosphereAmbientLUT.index);

    CloudResult result;
    result.L          = float3(0.0, 0.0, 0.0);
    result.throughput = float3(1.0, 1.0, 1.0);
    result.apDistance = 0.0;

    float rTopLayer    = planetRadiusMeter + topLayerMeter;
    float rBottomLayer = planetRadiusMeter + bottomLayerMeter;

    float2 bottomIntersection =
        RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rBottomLayer);
    float2 topIntersection =
        RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rTopLayer);

    float rayStart = bottomIntersection.y > 0.0 ? bottomIntersection.y : 0.0;
    float rayEnd   = topIntersection.y;

    bool bAboveCloudLayer = false;
    if (length(rayOrigin) < rBottomLayer)
    {
        // camera in-below cloud bottom layer
        rayStart = min(bottomIntersection.y, maxDistance);
        rayEnd   = min(topIntersection.y, maxDistance);
    }
    else if (length(rayOrigin) < rTopLayer)
    {
        // camera in-between cloud layers
        rayStart = 0.0;
        rayEnd   = bottomIntersection.x > 0.0 ? min(bottomIntersection.x, maxDistance) : min(topIntersection.y, maxDistance);
    }
    else if (topIntersection.x > 0.0)
    {
        // camera in-above cloud top layer
        rayStart = topIntersection.x;
        rayEnd   = min(bottomIntersection.x, maxDistance);

        bAboveCloudLayer = true;
    }
    else
    {
        // no cloud intersection
        return result;
    }

    float rayLength = rayEnd - rayStart;
    if (rayLength <= 0.0)
        return result;

    float3 sunDirection = normalize(float3(-Atmosphere.light.dirX, -Atmosphere.light.dirY, -Atmosphere.light.dirZ));
    float3 lightColor   = float3(Atmosphere.light.colorR, Atmosphere.light.colorG, Atmosphere.light.colorB);
    if (Atmosphere.light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(Atmosphere.light.temperatureK);

    float3 E = Atmosphere.light.illuminanceLux * lightColor * Cloud.scatteringScale;

    float VoL = dot(rayDirection, sunDirection);

    // --- Phase function --- //
    //float phase = phase_DualLob(VoL, CLOUD_FORWARD_SCATTERING_G, CLOUD_BACKWARD_SCATTERING_G, CLOUD_PHASE_BLEND_ALPHA);
    float baseScatter   = phase_DraineHG(VoL, 0.9881, 0.5567, 21.9955, 0.4824);
    float silverScatter = 0.5 * phase_HG(VoL, Cloud.silverScatterG);

    float phase = max(baseScatter, silverScatter);

    ParticipatingMediaPhaseContext PMPC = SetupParticipatingMediaPhaseContext(phase, Cloud.msEccentricity);
    // ---------------------- //

    // --- Sampling setup --- //
    float stepSize = rayLength / float(g_NumCloudRaymarchSteps);
    float tSample  = rayStart + stepSize * jitter.r;

    float3 movement = Cloud.windDirection * g_TimeSec * Cloud.windSpeedMps;
    // ---------------------- //

    float apScale       = 0.0;
    float apDistanceAcc = 0.0;

    float3 L          = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);
    for (int i = 0; i < g_NumCloudRaymarchSteps; ++i)
    {
        float3 samplePos    = rayOrigin + tSample * rayDirection;
        float  sampleTheta  = dot(normalize(samplePos), sunDirection);
        float  sampleHeight = length(samplePos);

        float altitude = sampleHeight - planetRadiusMeter;
        float hNorm    = inverseLerp(altitude, bottomLayerMeter, topLayerMeter);
        if (hNorm <= 0.0 || hNorm >= 1.0)
        {
            tSample += stepSize;
            continue;
        }

        float3 samplePosInLayer    = float3(samplePos.x, altitude, samplePos.z);
        float3 conservativeDensity = SampleCloudConservativeDensity(samplePosInLayer, hNorm, Cloud);
        if (conservativeDensity.x <= 0.0)
        {
            tSample += stepSize;
            continue;
        }

        float distToCamera = length(samplePosInLayer - g_Camera.posWORLD);
        CloudExtinctionResult CloudExtinction = SampleCloudExtinction(samplePosInLayer, hNorm, distToCamera, conservativeDensity, false, Cloud);
        if (CloudExtinction.extinction > 0.0f)
        {
            float3 atmosphereTransmittance = SampleTransmittanceLUT(TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, planetRadiusMeter, atmosphereRadiusMeter);

            // --- Prepare AerialPerspective --- //
            float apWeight = min(throughput.r, min(throughput.g, throughput.b));
            apDistanceAcc += tSample * apWeight;
            apScale       += apWeight;
            // --------------------------------- //
            float3 stepExtinction = CloudExtinction.extinction;

            float msScatteringStrength = Cloud.msContribution;
            float msExtinctionStrength = Cloud.msOcclusion;

            ParticipatingMediaExtinctionContext    PMEC = SetupParticipatingMediaExtinctionContext(Cloud.cloudAlbedo, stepExtinction, msScatteringStrength, msExtinctionStrength);
            ParticipatingMediaTransmittanceContext PMTC = RaymarchLight(samplePos, sunDirection, VoL, msExtinctionStrength, jitter.g);

            float  ambientU   = inverseLerp(sampleHeight, planetRadiusMeter, atmosphereRadiusMeter);
            float3 ambientLit = CalculateCloudAmbient(AtmosphereAmbientLUT.Sample(g_LinearWrapSampler, ambientU).rgb, hNorm, CloudExtinction.emissiveLerpAlpha);
            // --- Ground Contribution --- //
            if (Cloud.groundContributionStrength > 0.0)
            {
                float3 Nground = normalize(samplePos - PLANET_CENTER);
                float3 Dground = -Nground;

                const float groundRayLength    = altitude;
                const float groundStepCount    = 5.0;
                const float invGroundStepCount = 1.0 / groundStepCount;

                float tPrev = 0.0;
                float3 ODground = float3(0.0, 0.0, 0.0);
                for (float tGround = invGroundStepCount; tGround <= 1.00001; tGround += invGroundStepCount)
                {
                    float tCurr  = tGround * tGround;
                    float tDelta = tCurr - tPrev;

                    float  tSampleGround   = groundRayLength * (tCurr - 0.5 * tDelta);
                    float3 samplePosGround = samplePos + Dground * tSampleGround;

                    float altitudeGround = length(samplePosGround) - planetRadiusMeter;
                    float hNormGround    = inverseLerp(altitudeGround, bottomLayerMeter, topLayerMeter);

                    float distToCam     = length(samplePosGround - g_Camera.posWORLD);
                    float densityGround = SampleCloudExtinction(samplePosGround, hNormGround, distToCam, conservativeDensity, true, Cloud).extinction;
                    if (densityGround > 0.0)
                    {
                        float stepLength = groundRayLength * tDelta;

                        ODground += densityGround * stepLength;
                    }
                    tPrev = tCurr;
                }

                //float3 GoL = saturate(dot(sunDirection, Nground)) * (Atmosphere.groundAlbedo.rgb / PI);
                float3 GoL                   = (1.0 - saturate(dot(sunDirection, Nground))) * (Atmosphere.groundAlbedo.rgb / PI);
                float3 groundToCloudTransfer = (2.0 * PI) * IsotropicPhase() * GoL;

                float3 L0ground = E * atmosphereTransmittance * groundToCloudTransfer;

                ambientLit += L0ground * exp(-ODground) * Cloud.groundContributionStrength;
            }
            // -------------------------- //

            for (int ms = SCATTERING_OCTAVES - 1; ms >= 0; --ms)
            {
                float3 skyAmbient = ms == 0 ? ambientLit : 0.0;

                float3 extinction        = max(PMEC.cExtinction[ms], 1e-8);
                float3 opticalDepth      = extinction * stepSize;
                float3 stepTransmittance = exp(-opticalDepth);

                float3 S0    = (skyAmbient + E * atmosphereTransmittance * PMTC.transmittanceToLight0[ms] * PMPC.phase0[ms]) * PMEC.cScattering[ms];
                float3 S0int = (S0 - S0 * stepTransmittance) / extinction;

                L += throughput * S0int;
                if (ms == 0)
                {
                    throughput *= stepTransmittance;
                }
            }
        }

        if (all(float3(EPSILON_MIN, EPSILON_MIN, EPSILON_MIN) > throughput))
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
    RWTexture2D< float4 > OutCloudScatteringLUT = GetResource(g_OutCloudScatteringLUT.index);

    uint2 imgSize;
    uint2 pixCoords = tID.xy;
    OutCloudScatteringLUT.GetDimensions(imgSize.x, imgSize.y);
    if (tID.x >= imgSize.x || tID.y >= imgSize.y)
        return;

    Texture2D< float >    DepthBuffer          = GetResource(g_DepthBuffer.index);
    RWTexture2D< float2 > BlueNoiseLUT         = GetResource(g_BlueNoiseLUT.index);
    Texture3D< float4 >   AerialPerspectiveLUT = GetResource(g_AerialPerspectiveLUT.index);

    float2 uv       = float2(tID.xy + 0.5) / float2(imgSize);
    float  depth    = DepthBuffer.Sample(g_PointClampSampler, uv).r;

    CloudData      Cloud      = GetCloudData();
    AtmosphereData Atmosphere = GetAtmosphereData();

    const float topLayerMeter     = Cloud.topLayerKm * KM_TO_M;
    const float bottomLayerMeter  = Cloud.bottomLayerKm * KM_TO_M;
    const float planetRadiusMeter = Atmosphere.planetRadiusKm * KM_TO_M;

    float3 cameraPosAbovePlanet =
        g_Camera.posWORLD + float3(0.0, planetRadiusMeter, 0.0);

    float3 posWORLD     = ReconstructWorldPos(uv, depth, g_Camera.mViewProjInv);
    float3 rayDirection = normalize(posWORLD);
    float3 rayOrigin    = cameraPosAbovePlanet;

    float maxDistance = depth == 0.0 ? RAY_MARCHING_MAX_DISTANCE : length(posWORLD);
    float rayAltitude = inverseLerp(rayOrigin.y - planetRadiusMeter, bottomLayerMeter, topLayerMeter);
    if (rayAltitude > 1.0)
    {
        // More cloud sight range as higher view
        maxDistance = lerp(maxDistance, 1e8, rayAltitude * 0.1);
    }

    float2 jitter;
    {
        uint2 noiseTexSize;
        BlueNoiseLUT.GetDimensions(noiseTexSize.x, noiseTexSize.y);

        int3 moduloMasks = int3(
            (1u << floorLog2(noiseTexSize.x)) - 1,
            (1u << floorLog2(128)) - 1,
            (1u << floorLog2(64)) - 1);

        uint2 fullResPixelCoords = uint2((float2(pixCoords) + 0.5) * 4.0);
        uint3 wrappedCoordinate  = uint3(fullResPixelCoords, g_Frame) & moduloMasks;
        int2  noiseUV            = int2(wrappedCoordinate.x, wrappedCoordinate.z * 128.0 + wrappedCoordinate.y);

        jitter = BlueNoiseLUT.Load(noiseUV).rr;
    }

    CloudResult cloud = RaymarchCloud(rayOrigin, rayDirection, maxDistance, jitter);
    float3 L = cloud.L;
    float  A = dot(cloud.throughput, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
    if (cloud.apDistance > 0.0)
    {
        // Aerial perspective
        {
            float viewDistanceKm = cloud.apDistance * M_TO_KM;

            uint3 apSize;
            AerialPerspectiveLUT.GetDimensions(apSize.x, apSize.y, apSize.z);

            float slice = viewDistanceKm * (1.0 / AP_KM_PER_SLICE);
            float w     = sqrt(slice / (float)apSize.z);
                  slice = w * apSize.z;

            float4 ap = AerialPerspectiveLUT.Sample(g_LinearClampSampler, float3(uv, w));

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

    OutCloudScatteringLUT[pixCoords] = float4(L, A);
}