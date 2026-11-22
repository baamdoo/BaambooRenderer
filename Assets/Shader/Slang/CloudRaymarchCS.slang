#include "ShaderShared/Scene.h"
#include "Common/Binding.slangh"
#include "Common/Math.slangh"
#include "Common/Color.slangh"
#include "Common/Atmosphere.slangh"
#include "Common/Cloud.slangh"

struct CloudRaymarchPush
{
    CloudRaymarchConstants constants;

    uint ID_Scene;
    uint ID_CloudMacroMap;
    uint ID_CloudBaseNoise;
    uint ID_CloudProfileLUT;
    uint ID_DepthBuffer;
    uint ID_TransmittanceLUT;
    uint ID_AerialPerspectiveLUT;
    uint ID_AtmosphereAmbientLUT;
    uint ID_BlueNoiseLUT;
    uint ID_OutCloudScatteringLUT;
};

[[vk::push_constant]] ConstantBuffer< CloudRaymarchPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

static const int MS_OCTAVES         = 2;
static const int SCATTERING_OCTAVES = 1 + MS_OCTAVES;


////////////////////////////////////////////////////////////////////////////
// Phase //
// Reference: https://research.nvidia.com/labs/rtr/approximate-mie/
struct ParticipatingMediaPhaseContext
{
    float phase0[SCATTERING_OCTAVES];
};

ParticipatingMediaPhaseContext SetupParticipatingMediaPhaseContext(float basePhase0, float baseMsPhaseStrength)
{
    ParticipatingMediaPhaseContext PMPC;
    PMPC.phase0[0] = basePhase0;

    const float isoPhase        = IsotropicPhase();
    float       msPhaseStrength = baseMsPhaseStrength;

    // becomes isotropic-phase as the scattering level increases
    for (int ms = 1; ms < SCATTERING_OCTAVES; ++ms)
    {
        PMPC.phase0[ms] = lerp(isoPhase, PMPC.phase0[0], msPhaseStrength);
        msPhaseStrength *= msPhaseStrength;
    }

    return PMPC;
}

float phase_HG(float cosTheta, float g)
{
    const float g2    = g * g;
    const float num   = 1.0 - g2;
    const float denom = 4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);

    return num / denom;
}

float phase_Draine(float cosTheta, float g, float a)
{
    const float g2    = g * g;
    const float num   = (1.0 - g2) * (1.0 + a * cosTheta * cosTheta);
    const float denom = (1.0 + (a * (1.0 + 2.0 * g2)) / 3.0) * 4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);

    return num / denom;
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
};

struct ParticipatingMediaExtinctionContext
{
    float3 cScattering[SCATTERING_OCTAVES];
    float3 cExtinction[SCATTERING_OCTAVES];
};

ParticipatingMediaExtinctionContext SetupParticipatingMediaExtinctionContext(float3 baseAlbedo, float3 baseExtinction, float msScattering, float msExtinction)
{
    ParticipatingMediaExtinctionContext PMEC;
    PMEC.cScattering[0] = baseAlbedo * baseExtinction;
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

float3 CalculateCloudAmbient(CloudData cloud, float3 baseAmbient, float hNorm, float alpha)
{
    const float3 muddyGreyTint = float3(0.25, 0.20, 0.20);

    const float topIntensity    = 1.0;
    const float bottomIntensity = lerp(0.25, 0.75, hNorm) * 2.55;

    const float3 ambientOvercasted  = lerp(baseAmbient, baseAmbient * muddyGreyTint, cloud.localOvercast);
    const float3 ambientDesaturated = Desaturate(ambientOvercasted, 0.2);

    return ambientDesaturated * cloud.ambientIntensity * lerp(topIntensity, bottomIntensity, alpha);
}

ParticipatingMediaTransmittanceContext RaymarchLight(CloudVolume clouds, float planetRadiusMeter, float3 rayOrigin, float3 rayDirection, float msExtinctionStrength)
{
    const float topLayerMeter          = clouds.data.topLayerKm * KM_TO_M;
    const float bottomLayerMeter       = clouds.data.bottomLayerKm * KM_TO_M;
    const float shadowMarchLengthMeter = clouds.data.shadowTracingDistanceKm * KM_TO_M;
    const uint  numSteps               = g_Push.constants.numLightRaymarchSteps;

    float3 opticalDepth[SCATTERING_OCTAVES];
    for (int ms = 0; ms < SCATTERING_OCTAVES; ++ms)
        opticalDepth[ms] = float3(0.0);

    float tPrev = 0.0;
    for (uint i = 1; i <= numSteps; ++i)
    {
        // non-linear shadow sample distribution
        const float st      = float(i) / float(numSteps);
        const float tCurr   = st * st;
        const float tDelta  = tCurr - tPrev;
        const float tShadow = shadowMarchLengthMeter * (tCurr - 0.5 * tDelta);
        tPrev = tCurr;

        const float3 spos = rayOrigin + tShadow * rayDirection;

        const float saltitude = length(spos) - planetRadiusMeter;
        const float shNorm    = inverseLerp(saltitude, bottomLayerMeter, topLayerMeter);
        if (shNorm > 1.0)
            break;
        if (shNorm < 0.0)
            continue;

        const float3 sposInLayer         = float3(spos.x, saltitude, spos.z);
        const float3 conservativeDensity = SampleCloudConservativeDensity(clouds, sposInLayer, shNorm);
        if (conservativeDensity.x <= 0.0)
            continue;

        float extinction = SampleCloudExtinction(clouds, sposInLayer, conservativeDensity, false).extinction;
        float msStrength = msExtinctionStrength;
        for (int ms = 0; ms < SCATTERING_OCTAVES; ++ms)
        {
            opticalDepth[ms] += extinction * tDelta;

            extinction *= msStrength;
            msStrength *= msStrength;
        }
    }

    ParticipatingMediaTransmittanceContext PMTC;
    for (int ms = 0; ms < SCATTERING_OCTAVES; ++ms)
        PMTC.transmittanceToLight0[ms] = exp(-opticalDepth[ms] * shadowMarchLengthMeter);

    return PMTC;
}

// Optical depth from the sample straight down to the cloud floor
float GroundOpticalDepth(CloudVolume clouds, float planetRadiusMeter, float3 samplePos, float altitude, float3 conservativeDensity)
{
    const uint  groundStepCount  = 5;
    const float bottomLayerMeter = clouds.data.bottomLayerKm * KM_TO_M;
    const float groundRayLength  = altitude - bottomLayerMeter;
    const float3 Dground         = -normalize(samplePos - PLANET_CENTER);

    float tPrev    = 0.0;
    float ODground = 0.0;
    for (uint i = 1; i <= groundStepCount; ++i)
    {
        const float st     = float(i) / float(groundStepCount);
        const float tCurr  = st * st;
        const float tDelta = tCurr - tPrev;
        tPrev = tCurr;

        const float3 samplePosGround = samplePos + Dground * (groundRayLength * (tCurr - 0.5 * tDelta));
        const float3 samplePosGroundInLayer = float3(samplePosGround.x, length(samplePosGround) - planetRadiusMeter, samplePosGround.z);

        const float densityGround = SampleCloudExtinction(clouds, samplePosGroundInLayer, conservativeDensity, true).extinction;
        if (densityGround > 0.0)
            ODground += densityGround * groundRayLength * tDelta;
    }
    return ODground;
}

struct CloudResult
{
    float3 L;          // Luminance
    float3 throughput; // Transmittance
    float  apDistance; // Weighted Average Distance for AP
};

CloudResult RaymarchCloud(
    CloudVolume clouds, AtmosphereData atmosphere, Texture2D< float3 > transmittanceLUT, Texture1D< float3 > ambientLUT,
    float3 rayOrigin, float3 rayDirection, float maxDistance, float jitter)
{
    const float topLayerMeter         = clouds.data.topLayerKm * KM_TO_M;
    const float bottomLayerMeter      = clouds.data.bottomLayerKm * KM_TO_M;
    const float planetRadiusMeter     = atmosphere.planetRadiusKm * KM_TO_M;
    const float atmosphereRadiusMeter = atmosphere.atmosphereRadiusKm * KM_TO_M;

    CloudResult result;
    result.L          = float3(0.0);
    result.throughput = float3(1.0);
    result.apDistance = 0.0;

    const float rTopLayer    = planetRadiusMeter + topLayerMeter;
    const float rBottomLayer = planetRadiusMeter + bottomLayerMeter;

    const float2 bottomIntersection = RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rBottomLayer);
    const float2 topIntersection    = RaySphereIntersection(rayOrigin, rayDirection, PLANET_CENTER, rTopLayer);

    float rayStart;
    float rayEnd;
    const float originRadius = length(rayOrigin);
    if (originRadius < rBottomLayer)
    {
        // camera in-below cloud bottom layer
        rayStart = min(bottomIntersection.y, maxDistance);
        rayEnd   = min(topIntersection.y, maxDistance);
    }
    else if (originRadius < rTopLayer)
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
    }
    else
    {
        // no cloud intersection
        return result;
    }

    const float rayLength = rayEnd - rayStart;
    if (rayLength <= 0.0)
        return result;

    const float distanceFade = 1.0 - smoothstep(18000.0, 60000.0, rayStart);
    if (distanceFade <= 0.0)
        return result;

    const float3 sunDirection = normalize(-atmosphere.light.direction);
    float3 lightColor = atmosphere.light.color;
    if (atmosphere.light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(atmosphere.light.temperatureK);

    const float3 E = atmosphere.light.illuminanceLux * lightColor;

    const float VoL = dot(rayDirection, sunDirection);

    // --- Phase function --- //
    const float baseScatter   = phase_DraineHG(VoL, 0.9881, 0.5567, 21.9955, 0.4824);
    const float silverScatter = 0.5 * phase_HG(VoL, 0.99);
    const float backScatter   = 0.5 * phase_HG(VoL, -0.15);

    const float phase = max(baseScatter, max(silverScatter, backScatter));

    const ParticipatingMediaPhaseContext PMPC = SetupParticipatingMediaPhaseContext(phase, 0.4);
    // ---------------------- //

    const uint  numSteps = g_Push.constants.numCloudRaymarchSteps;
    const float stepSize = rayLength / float(numSteps);
    float       tSample  = rayStart + stepSize * jitter;

    float apScale       = 0.0;
    float apDistanceAcc = 0.0;

    float3 L          = float3(0.0);
    float3 throughput = float3(1.0);
    for (uint i = 0; i < numSteps; ++i, tSample += stepSize)
    {
        const float3 samplePos    = rayOrigin + tSample * rayDirection;
        const float  sampleTheta  = dot(normalize(samplePos), sunDirection);
        const float  sampleHeight = length(samplePos);

        const float altitude = sampleHeight - planetRadiusMeter;
        const float hNorm    = inverseLerp(altitude, bottomLayerMeter, topLayerMeter);
        if (hNorm <= 0.0 || hNorm >= 1.0)
            continue;

        const float3 samplePosInLayer    = float3(samplePos.x, altitude, samplePos.z);
        const float3 conservativeDensity = SampleCloudConservativeDensity(clouds, samplePosInLayer, hNorm);
        if (conservativeDensity.x <= 0.0)
            continue;

        const CloudExtinctionResult cloudExtinction = SampleCloudExtinction(clouds, samplePosInLayer, conservativeDensity, false);
        if (cloudExtinction.extinction > 0.0)
        {
            const float3 atmosphereTransmittance = SampleTransmittanceLUT(transmittanceLUT, sampleHeight, sampleTheta, planetRadiusMeter, atmosphereRadiusMeter);

            // --- Prepare AerialPerspective --- //
            const float apWeight = min(throughput.r, min(throughput.g, throughput.b));
            apDistanceAcc += tSample * apWeight;
            apScale       += apWeight;
            // --------------------------------- //

            const float msScatteringStrength = clouds.data.msContribution;
            const float msExtinctionStrength = clouds.data.msOcclusion;

            const ParticipatingMediaExtinctionContext    PMEC = SetupParticipatingMediaExtinctionContext(float3(1.0), float3(cloudExtinction.extinction), msScatteringStrength, msExtinctionStrength);
            const ParticipatingMediaTransmittanceContext PMTC = RaymarchLight(clouds, planetRadiusMeter, samplePos, sunDirection, msExtinctionStrength);

            const float ambientU = inverseLerp(sampleHeight, planetRadiusMeter, atmosphereRadiusMeter);
            float3 ambientLit = CalculateCloudAmbient(clouds.data, ambientLUT.SampleLevel(g_LinearClampSampler, ambientU, 0).rgb, hNorm, cloudExtinction.emissiveLerpAlpha);

            // --- Ground Contribution --- //
            if (clouds.data.groundContributionStrength > 0.0)
            {
                const float3 Nground  = normalize(samplePos - PLANET_CENTER);
                const float  ODground = GroundOpticalDepth(clouds, planetRadiusMeter, samplePos, altitude, conservativeDensity);

                const float3 GoL                   = (1.0 - saturate(dot(sunDirection, Nground))) * (atmosphere.groundAlbedo / PI);
                const float3 groundToCloudTransfer = (2.0 * PI) * IsotropicPhase() * GoL;

                const float3 L0ground = E * atmosphereTransmittance * groundToCloudTransfer;

                ambientLit += L0ground * exp(-ODground) * clouds.data.groundContributionStrength;
            }
            // -------------------------- //

            for (int ms = SCATTERING_OCTAVES - 1; ms >= 0; --ms)
            {
                const float3 skyAmbient = ms == 0 ? ambientLit : float3(0.0);

                const float3 extinction        = max(PMEC.cExtinction[ms], float3(1e-8));
                const float3 opticalDepth      = extinction * stepSize;
                const float3 stepTransmittance = exp(-opticalDepth);

                const float3 S0    = (skyAmbient + E * atmosphereTransmittance * PMTC.transmittanceToLight0[ms] * PMPC.phase0[ms]) * PMEC.cScattering[ms];
                const float3 S0int = (S0 - S0 * stepTransmittance) / extinction;

                L += throughput * S0int;
                if (ms == 0)
                    throughput *= stepTransmittance;
            }
        }

        if (all(throughput < float3(EPSILON_MIN)))
            break;
    }

    result.L          = L * distanceFade;
    result.throughput = lerp(float3(1.0), throughput, distanceFade);
    if (apScale > 0.0)
        result.apDistance = apDistanceAcc / apScale;

    return result;
}

// Tiles of W x W blue noise stacked vertically, one tile per frame
float LoadBlueNoise(Texture2D< float > blueNoise, uint2 pixel, uint frameIndex)
{
    uint2 noiseSize;
    blueNoise.GetDimensions(noiseSize.x, noiseSize.y);

    const uint  tileSize  = noiseSize.x;
    const uint  tileCount = max(noiseSize.y / tileSize, 1u);
    const uint2 texel     = pixel % tileSize;
    const uint  tile      = frameIndex % tileCount;
    return blueNoise.Load(int3(texel.x, tile * tileSize + texel.y, 0));
}

[numthreads(8, 8, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2D< float4 > OutCloudScatteringLUT = GetResource(g_Push.ID_OutCloudScatteringLUT);

    uint2 imageSize;
    OutCloudScatteringLUT.GetDimensions(imageSize.x, imageSize.y);
    const uint2 pixCoords = tID.xy;
    if (any(pixCoords >= imageSize))
        return;

    Texture2D< float >  DepthBuffer          = GetResource(g_Push.ID_DepthBuffer);
    Texture2D< float >  BlueNoiseLUT         = GetResource(g_Push.ID_BlueNoiseLUT);
    Texture3D< float4 > AerialPerspectiveLUT = GetResource(g_Push.ID_AerialPerspectiveLUT);
    Texture2D< float3 > TransmittanceLUT     = GetResource(g_Push.ID_TransmittanceLUT);
    Texture1D< float3 > AtmosphereAmbientLUT = GetResource(g_Push.ID_AtmosphereAmbientLUT);

    StructuredBuffer< SceneGlobals > scene = GetResource(g_Push.ID_Scene);
    const float4x4       mViewProjInv = scene[0].camera.mViewProjInv;
    const float3         cameraPos    = scene[0].camera.position;
    const AtmosphereData atmosphere   = scene[0].environment.atmosphere;

    CloudVolume clouds;
    clouds.data       = scene[0].environment.cloud;
    clouds.macroMap   = GetResource(g_Push.ID_CloudMacroMap);
    clouds.baseNoise  = GetResource(g_Push.ID_CloudBaseNoise);
    clouds.profileLUT = GetResource(g_Push.ID_CloudProfileLUT);

    const float2 uv    = (float2(pixCoords) + 0.5) / float2(imageSize);
    const float  depth = DepthBuffer.SampleLevel(g_PointClampSampler, uv, 0);

    const float topLayerMeter     = clouds.data.topLayerKm * KM_TO_M;
    const float bottomLayerMeter  = clouds.data.bottomLayerKm * KM_TO_M;
    const float planetRadiusMeter = atmosphere.planetRadiusKm * KM_TO_M;

    // Reverse-Z sky (depth 0) lands at w ~ 0, where the undivided xyz is the view direction
    const float4 posWS   = mul(mViewProjInv, float4(uv.x * 2.0 - 1.0, uv.y * -2.0 + 1.0, depth, 1.0));
    const float3 viewVec = depth == 0.0 ? posWS.xyz : posWS.xyz / posWS.w - cameraPos;

    const float3 rayOrigin    = cameraPos + float3(0.0, planetRadiusMeter, 0.0);
    const float3 rayDirection = normalize(viewVec);

    float maxDistance = depth == 0.0 ? RAY_MARCHING_MAX_DISTANCE : length(viewVec);
    const float rayAltitude = inverseLerp(cameraPos.y, bottomLayerMeter, topLayerMeter);
    if (depth == 0.0 && rayAltitude > 1.0)
    {
        // More cloud sight range as higher view (sky pixels only; geometry keeps its surface clamp)
        maxDistance = lerp(maxDistance, 1e8, rayAltitude * 0.1);
    }

    const float jitter = LoadBlueNoise(BlueNoiseLUT, pixCoords, g_Push.constants.frameIndex);

    const CloudResult cloud = RaymarchCloud(clouds, atmosphere, TransmittanceLUT, AtmosphereAmbientLUT, rayOrigin, rayDirection, maxDistance, jitter);

    float3 L = cloud.L;
    const float A = dot(cloud.throughput, float3(1.0 / 3.0));
    if (cloud.apDistance > 0.0)
    {
        // Aerial perspective
        const float viewDistanceKm = cloud.apDistance * M_TO_KM;

        uint3 apSize;
        AerialPerspectiveLUT.GetDimensions(apSize.x, apSize.y, apSize.z);

        // Inverse of the aerial-perspective writer, which spaces slices by w^2
        float slice   = viewDistanceKm * (1.0 / AP_KM_PER_SLICE);
        const float w = sqrt(slice / float(apSize.z));
        slice         = w * float(apSize.z);

        float4 ap = AerialPerspectiveLUT.SampleLevel(g_LinearClampSampler, float3(uv, w), 0);

        // prevents an abrupt appearance of fog on objects close to the camera
        float weight = 1.0;
        if (slice < sqrt(0.5))
            weight = clamp(slice * slice * 2.0, 0.0, 1.0);

        ap.rgb *= weight;
        ap.a = 1.0 - weight * (1.0 - ap.a);

        // FinalColor = (SurfaceColor * Transmittance) + InScatteredLight
        L = L * ap.a + ap.rgb * (1.0 - A); // Apply (1.0 - A) to prevent neglectable noise values from meeting with AP and being amplified
    }

    OutCloudScatteringLUT[pixCoords] = float4(L, A);
}
