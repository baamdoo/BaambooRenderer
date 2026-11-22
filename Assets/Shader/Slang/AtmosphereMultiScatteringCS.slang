#include "ShaderShared/Scene.h"
#include "Common/Atmosphere.slangh"

struct AtmosphereMultiScatteringPush
{
    AtmosphereRaymarchConstants constants;

    uint ID_Scene;
    uint ID_TransmittanceLUT;
    uint ID_OutMultiScatteringLUT;
};

[[vk::push_constant]] ConstantBuffer< AtmosphereMultiScatteringPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

float3 ComputeMultiScattering(
    AtmosphereData atmosphere, Texture2D< float3 > transmittanceLUT,
    float viewHeight, float cosZenithAngle, uint isoSampleCount, uint numRaySteps)
{
    const float3 rayOrigin = float3(0.0, viewHeight, 0.0);
    const float3 sunDir    = float3(safeSqrt(1.0 - cosZenithAngle * cosZenithAngle), cosZenithAngle, 0.0);

    const float weight       = (4.0 * PI) / float(isoSampleCount);
    const float uniformPhase = IsotropicPhase();

    float3 L2ndOrder = float3(0.0);
    float3 fms       = float3(0.0);

    // uniform sphere sampling (isotropic phase function)
    const float goldenRatio = (1.0 + sqrt(5.0)) / 2.0;
    for (uint i = 0u; i < isoSampleCount; ++i)
    {
        const float theta = 2.0 * PI * frac(float(i) / goldenRatio);
        const float phi   = acos(1.0 - 2.0 * (float(i) + 0.5) / float(isoSampleCount));

        const float3 rayDir = float3(
            sin(phi) * cos(theta),
            cos(phi),
            sin(phi) * sin(theta)
        );

        const float2 planetIntersection     = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, atmosphere.planetRadiusKm);
        const float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, atmosphere.atmosphereRadiusKm);
        if (atmosphereIntersection.y < 0.0)
            continue;

        const bool  bHitGround = planetIntersection.x >= 0.0;
        const float rayLength  = bHitGround ? planetIntersection.x : atmosphereIntersection.y;
        const float stepSize   = rayLength / float(numRaySteps);

        float3 L          = float3(0.0);
        float3 Lf         = float3(0.0);
        float3 throughput = float3(1.0);
        for (uint j = 0u; j < numRaySteps; ++j)
        {
            const float  t   = (float(j) + 0.5) * stepSize;
            const float3 pos = rayOrigin + t * rayDir;

            const float sampleHeight = length(pos);
            if (sampleHeight < atmosphere.planetRadiusKm)
                break;

            const AtmosphereMedium medium = SampleAtmosphereMedium(atmosphere, sampleHeight - atmosphere.planetRadiusKm);
            const float3 stepTransmittance = exp(-medium.extinction * stepSize);

            const float  cosSunZenith       = dot(pos / sampleHeight, sunDir);
            const float3 transmittanceToSun = SampleTransmittanceLUT(transmittanceLUT, sampleHeight, cosSunZenith, atmosphere.planetRadiusKm, atmosphere.atmosphereRadiusKm);

            const float planetShadow = RaySphereIntersection(pos, sunDir, PLANET_CENTER, atmosphere.planetRadiusKm).x < 0.0 ? 1.0 : 0.0;

            // (4) S(x,l) = Vis(l) * T(x,x+t*l)
            const float3 S = planetShadow * transmittanceToSun;

            L          += medium.scattering * throughput * S * uniformPhase * stepSize; // (6) L'(x,v) = σs(x) * T(x,x-tv) * S(x,ws) * pu * EI * dt
            Lf         += medium.scattering * throughput * stepSize;                    // (8) Lf(x,v) = σs(x) * T(x,x-tv) * 1 * dt
            throughput *= stepTransmittance;
        }

        // light reflected by the ground: T(x,p) * Lo(p,v) with a Lambertian albedo
        if (bHitGround)
        {
            const float3 pos          = rayOrigin + planetIntersection.x * rayDir;
            const float  sampleHeight = length(pos);
            const float  cosSunZenith = dot(pos / sampleHeight, sunDir);

            const float3 transmittanceToSun = SampleTransmittanceLUT(transmittanceLUT, sampleHeight, cosSunZenith, atmosphere.planetRadiusKm, atmosphere.atmosphereRadiusKm);
            L += throughput * transmittanceToSun * saturate(cosSunZenith) * atmosphere.groundAlbedo / PI;
        }

        // (5) L2ndOrder_i = L'(xs, -w) * pu * dw
        L2ndOrder += (L * uniformPhase) * weight;

        // (7) fms_i = Lf(xs, -w) * pu * dw
        fms += (Lf * uniformPhase) * weight;
    }

    // after the 2nd order the scattering is close to isotropic, so the series 1 + f + f^2 + ... sums to 1 / (1 - f)
    return L2ndOrder / (1.0 - fms);
}

[numthreads(8, 8, 1)]
void main(uint2 tID : SV_DispatchThreadID)
{
    RWTexture2D< float3 > OutMultiScatteringLUT = GetResource(g_Push.ID_OutMultiScatteringLUT);

    uint2 texSize;
    OutMultiScatteringLUT.GetDimensions(texSize.x, texSize.y);
    if (any(tID >= texSize))
        return;

    StructuredBuffer< SceneGlobals > sceneBuf = GetResource(g_Push.ID_Scene);
    const AtmosphereData atmosphere = sceneBuf[0].environment.atmosphere;

    Texture2D< float3 > TransmittanceLUT = GetResource(g_Push.ID_TransmittanceLUT);

    const float2 uv = (float2(tID) + 0.5) / float2(texSize);

    const float cosZenithAngle = 2.0 * uv.x - 1.0;
    const float viewHeight     = lerp(atmosphere.planetRadiusKm, atmosphere.atmosphereRadiusKm, uv.y);

    OutMultiScatteringLUT[tID] = ComputeMultiScattering(
        atmosphere, TransmittanceLUT, viewHeight, cosZenithAngle,
        g_Push.constants.msIsoSampleCount, g_Push.constants.msNumRaySteps);
}
