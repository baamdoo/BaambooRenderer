#define _SCENEENVIRONMENT
#include "AtmosphereCommon.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_MinRaySteps;
    uint g_MaxRaySteps;
    uint g_SampleCount;
};

ConstantBuffer< DescriptorHeapIndex > g_TransmittanceLUT        : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MultiScatteringLUT      : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutAtmosphereAmbientLUT : register(b3, ROOT_CONSTANT_SPACE);


float3 RaymarchScattering(float3 rayOrigin, float3 rayDir, float maxDistance)
{
    AtmosphereData Atmosphere = GetAtmosphereData();

    Texture2D< float3 > TransmittanceLUT   = GetResource(g_TransmittanceLUT.index);
    Texture2D< float3 > MultiScatteringLUT = GetResource(g_MultiScatteringLUT.index);

    float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, Atmosphere.atmosphereRadiusKm);
    if (atmosphereIntersection.y < 0.0)
        return float3(0.0, 0.0, 0.0);

    float rayStart  = max(0.0, atmosphereIntersection.x);
    float rayLength = min(maxDistance, atmosphereIntersection.y) - rayStart;
    if (rayLength <= 0.0)
        return float3(0.0, 0.0, 0.0);

    // light illuminance
    float3 lightColor = float3(Atmosphere.light.colorR, Atmosphere.light.colorG, Atmosphere.light.colorB);
    if (Atmosphere.light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(Atmosphere.light.temperatureK);

    float3 E = Atmosphere.light.illuminanceLux * lightColor;

    // phase functions
    float3 sunDirection = float3(-Atmosphere.light.dirX, -Atmosphere.light.dirY, -Atmosphere.light.dirZ);

    float cosTheta      = dot(rayDir, sunDirection);
    float phaseRayleigh = RayleighPhase(cosTheta);
    float phaseMie      = MiePhase(cosTheta, Atmosphere.miePhaseG);

    // variable sampling count according to rayLength
    float numSteps = lerp(float(g_MinRaySteps), float(g_MaxRaySteps), clamp(rayLength / 150.0, 0.0, 1.0));
    float stepSize = rayLength / numSteps;

    float3 L          = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);

    [loop]
    for (uint i = 0u; i < numSteps; ++i)
    {
        float t    = rayStart + (float(i) + 0.5) * stepSize;
        float3 pos = rayOrigin + t * rayDir;

        // skip if below ground
        float sampleHeight = length(pos);
        if (sampleHeight < Atmosphere.planetRadiusKm)
            break;
        // skip if above atmosphere
        if (sampleHeight > Atmosphere.atmosphereRadiusKm)
            continue;

        float sampleAltitude = sampleHeight - Atmosphere.planetRadiusKm;

        float rayleighDensity = GetDensityAtHeight(sampleAltitude, Atmosphere.rayleighDensityKm);
        float mieDensity      = GetDensityAtHeight(sampleAltitude, Atmosphere.mieDensityKm);
        float ozoneDensity    = GetDensityOzoneAtHeight(sampleAltitude, Atmosphere.ozoneCenterKm, Atmosphere.ozoneWidthKm);

        float3 rayleighScattering = Atmosphere.rayleighScattering * rayleighDensity;
        float  mieScattering      = Atmosphere.mieScattering * mieDensity;
        float  mieAbsorption      = Atmosphere.mieAbsorption * mieDensity;
        float3 ozoneAbsorption    = Atmosphere.ozoneAbsorption * ozoneDensity;

        float3 scattering       = rayleighScattering + float3(mieScattering, mieScattering, mieScattering);
        float3 extinction       = rayleighScattering + float3(mieScattering + mieAbsorption, mieScattering + mieAbsorption, mieScattering + mieAbsorption) + ozoneAbsorption;
        float3 phasedScattering = phaseRayleigh * rayleighScattering + phaseMie * mieScattering;
        // σs(x) * p(v,l)
        float3 stepTransmittance = exp(-extinction * stepSize);
        // (2) T(xa,xb) = exp(−Integral(xa~xb, σ(x)dx))

        // transmittance from sample point to sun
        float  sampleTheta = dot(normalize(pos), sunDirection);
        float3 transmittanceToSun = SampleTransmittanceLUT(TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, Atmosphere.planetRadiusKm, Atmosphere.atmosphereRadiusKm);

        // multi-scattering
        float2 msUV = clamp(
                float2(sampleTheta * 0.5 + 0.5, inverseLerp(sampleHeight, Atmosphere.planetRadiusKm, Atmosphere.atmosphereRadiusKm)),
            0.0, 1.0);

        float3 multiScattering = MultiScatteringLUT.Sample(g_LinearClampSampler, msUV).rgb;

        // planet shadow
        float2 planetIntersection = RaySphereIntersection(pos, sunDirection, PLANET_CENTER, Atmosphere.planetRadiusKm);
        float  planetShadow       = planetIntersection.x < 0.0 ? 1.0 : 0.0;

        // Analytical integration
        float3 S    = (planetShadow * transmittanceToSun * phasedScattering + multiScattering * scattering) * E;
        float3 Sint = (S - S * stepTransmittance) / max(extinction, 1e-8f);
        L += throughput * Sint;

        throughput *= stepTransmittance;
    }

    return L;
}

// hash
float rand(float2 co)
{
    return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453);
}

// Cosine-weighted hemisphere sampling
// Reference: https://ameye.dev/notes/sampling-the-hemisphere/
float3 GetHemisphereSampleCos(float3 N, uint i, uint Ns)
{
    float phi      = 2.0 * PI * (float(i) / float(Ns));
    float cosTheta = sqrt(1.0 - (float(i) + 0.5) / float(Ns));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    return tangent * H.x + bitangent * H.y + N * H.z;
}

float3 GetHemisphereSampleUniform(float3 N, uint i, uint Ns)
{
    // Hammersley
    float phi      = 2.0 * PI * (float(i) / float(Ns));
    float cosTheta = 1.0 - (float(i) + 0.5) / float(Ns);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    return tangent * H.x + bitangent * H.y + N * H.z;
}

[numthreads(64, 1, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture1D< float4 > OutAtmosphereAmbientLUT = GetResource(g_OutAtmosphereAmbientLUT.index);

    uint imgSize;
    uint pixCoords = tID.x;
    OutAtmosphereAmbientLUT.GetDimensions(imgSize);
    if (pixCoords >= imgSize)
        return;

    AtmosphereData Atmosphere = GetAtmosphereData();

    float t        = (float(pixCoords) + 0.5) / float(imgSize);
    float altitude = t * (Atmosphere.atmosphereRadiusKm - Atmosphere.planetRadiusKm);

    float3 rayOrigin = float3(0.0, Atmosphere.planetRadiusKm + altitude, 0.0);
    float3 up        = float3(0.0, 1.0, 0.0);

    uint   numSamples       = g_SampleCount;
    float3 totalIlluminance = float3(0.0, 0.0, 0.0);

    // Integral over hemisphere
    [loop]
    for (uint i = 0u; i < numSamples; ++i)
    {
        float3 rayDir = GetHemisphereSampleCos(up, i, numSamples);
        float  NoL    = dot(rayDir, up);

        // Ignore downward ray
        if (NoL <= 0.0)
            continue;

        float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, Atmosphere.planetRadiusKm);
        if (groundIntersection.x > 0.0)
        {
            // If ray touches the ground, assume this ray isn't inscattered 
            // (ignore scattering of ground albedo since this shader only consider sky light)
            continue;
        }

        float3 inscattered = RaymarchScattering(rayOrigin, rayDir, RAY_MARCHING_MAX_DISTANCE);

        // PDF(w_i) = PI / cos(w_i)
        // Lo = (1 / N) * Sum[ (Lin(w_i) * cos(w_i)) / PDF(w_i) ]
        // Lo = (PI / N) * Sum[ Lin(w_i) ]
        totalIlluminance += inscattered.rgb;
    }

    // normalization
    if (numSamples > 0)
    {
        totalIlluminance *= (PI / float(numSamples));
    }

    OutAtmosphereAmbientLUT[int(pixCoords)] = float4(totalIlluminance, 1.0);
}