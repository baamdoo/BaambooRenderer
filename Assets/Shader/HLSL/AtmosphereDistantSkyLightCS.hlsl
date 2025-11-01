#include "Common.hlsli"
#define _ATMOSPHERE
#include "AtmosphereCommon.hlsli"

Texture2D< float3 > g_TransmittanceLUT   : register(t0);
Texture2D< float3 > g_MultiScatteringLUT : register(t1);

RWTexture1D< float4 > g_AtmosphereAmbientLUT : register(u0);

SamplerState g_LinearClampSampler : register(SAMPLER_INDEX_LINEAR_CLAMP);

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_MinRaySteps;
    uint g_MaxRaySteps;
    uint g_SampleCount;
};


float3 RaymarchScattering(float3 rayOrigin, float3 rayDir, float maxDistance)
{
    float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Atmosphere.atmosphereRadius_km);
    if (atmosphereIntersection.y < 0.0)
        return float3(0.0, 0.0, 0.0);

    float rayStart  = max(0.0, atmosphereIntersection.x);
    float rayLength = min(maxDistance, atmosphereIntersection.y) - rayStart;
    if (rayLength <= 0.0)
        return float3(0.0, 0.0, 0.0);

    // light illuminance
    float3 lightColor = float3(g_Atmosphere.light.colorR, g_Atmosphere.light.colorG, g_Atmosphere.light.colorB);
    if (g_Atmosphere.light.temperature_K > 0.0)
        lightColor *= ColorTemperatureToRGB(g_Atmosphere.light.temperature_K);

    float3 E = g_Atmosphere.light.illuminance_lux * lightColor;

    // phase functions
    float cosTheta      = dot(rayDir, float3(-g_Atmosphere.light.dirX, -g_Atmosphere.light.dirY, -g_Atmosphere.light.dirZ));
    float phaseRayleigh = RayleighPhase(cosTheta);
    float phaseMie      = MiePhase(cosTheta, g_Atmosphere.miePhaseG);

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
        if (sampleHeight < g_Atmosphere.planetRadius_km)
            break;
        // skip if above atmosphere
        if (sampleHeight > g_Atmosphere.atmosphereRadius_km)
            continue;

        float sampleAltitude = sampleHeight - g_Atmosphere.planetRadius_km;

        float rayleighDensity = GetDensityAtHeight(sampleAltitude, g_Atmosphere.rayleighDensityH_km);
        float mieDensity      = GetDensityAtHeight(sampleAltitude, g_Atmosphere.mieDensityH_km);
        float ozoneDensity    = GetDensityOzoneAtHeight(sampleAltitude);

        float3 rayleighScattering = g_Atmosphere.rayleighScattering * rayleighDensity;
        float  mieScattering      = g_Atmosphere.mieScattering * mieDensity;
        float  mieAbsorption      = g_Atmosphere.mieAbsorption * mieDensity;
        float3 ozoneAbsorption    = g_Atmosphere.ozoneAbsorption * ozoneDensity;

        float3 scattering       = rayleighScattering + float3(mieScattering, mieScattering, mieScattering);
        float3 extinction       = rayleighScattering + float3(mieScattering + mieAbsorption, mieScattering + mieAbsorption, mieScattering + mieAbsorption) + ozoneAbsorption;
        float3 phasedScattering = phaseRayleigh * rayleighScattering + phaseMie * mieScattering;
        // σs(x) * p(v,l)
        float3 stepTransmittance = exp(-extinction * stepSize);
        // (2) T(xa,xb) = exp(−Integral(xa~xb, σ(x)dx))

        // transmittance from sample point to sun
        float  sampleTheta = dot(normalize(pos), float3(-g_Atmosphere.light.dirX, -g_Atmosphere.light.dirY, -g_Atmosphere.light.dirZ));
        float3 transmittanceToSun = SampleTransmittanceLUT(g_TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km);

        // multi-scattering
        float2 msUV = clamp(
                float2(sampleTheta * 0.5 + 0.5, inverseLerp(sampleHeight, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km)),
            0.0, 1.0);

        float3 multiScattering = g_MultiScatteringLUT.Sample(g_LinearClampSampler, msUV).rgb;

        // planet shadow
        float2 planetIntersection = RaySphereIntersection(pos, float3(-g_Atmosphere.light.dirX, -g_Atmosphere.light.dirY, -g_Atmosphere.light.dirZ), PLANET_CENTER, g_Atmosphere.planetRadius_km);
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
    uint imgSize;
    uint pixCoords = tID.x;
    g_AtmosphereAmbientLUT.GetDimensions(imgSize);
    if (pixCoords >= imgSize)
        return;

    float t        = (float(pixCoords) + 0.5) / float(imgSize);
    float altitude = t * (g_Atmosphere.atmosphereRadius_km - g_Atmosphere.planetRadius_km);

    float3 rayOrigin = float3(0.0, g_Atmosphere.planetRadius_km + altitude, 0.0);
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

        float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Atmosphere.planetRadius_km);
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

    g_AtmosphereAmbientLUT[int(pixCoords)] = float4(totalIlluminance, 1.0);
}