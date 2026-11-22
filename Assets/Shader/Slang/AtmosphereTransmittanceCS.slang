#include "ShaderShared/Scene.h"
#include "Common/Atmosphere.slangh"

struct AtmosphereTransmittancePush
{
    uint ID_Scene;
    uint ID_OutTransmittanceLUT;
};

[[vk::push_constant]] ConstantBuffer< AtmosphereTransmittancePush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

static const uint kNumOpticalDepthSamples = 64;

// Must stay the exact inverse of the (d, rho) mapping in SampleTransmittanceLUT
void GenerateTransmittanceCoordsFromUV(float2 uv, float bottomRadius, float topRadius, out float cosZenithAngle, out float viewHeight)
{
    const float H   = safeSqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    const float rho = H * uv.y;
    viewHeight = safeSqrt(rho * rho + bottomRadius * bottomRadius);

    const float d_min = topRadius - viewHeight;
    const float d_max = rho + H;
    const float d     = lerp(d_min, d_max, uv.x);

    cosZenithAngle = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * viewHeight * d);
    cosZenithAngle = clamp(cosZenithAngle, -1.0, 1.0);
}

float3 ComputeOpticalDepth(AtmosphereData atmosphere, float3 rayOrigin, float3 rayDir, float rayLength, uint numSamples)
{
    const float stepSize = rayLength / float(numSamples);

    float3 opticalDepth = float3(0.0);
    for (uint i = 0u; i < numSamples; ++i)
    {
        const float  t   = (float(i) + 0.5) * stepSize;
        const float3 pos = rayOrigin + t * rayDir;

        const AtmosphereMedium medium = SampleAtmosphereMedium(atmosphere, GetAltitude(pos, atmosphere.planetRadiusKm));
        opticalDepth += medium.extinction * stepSize;
    }

    return opticalDepth;
}

[numthreads(8, 8, 1)]
void main(uint2 tID : SV_DispatchThreadID)
{
    RWTexture2D< float3 > OutTransmittanceLUT = GetResource(g_Push.ID_OutTransmittanceLUT);

    uint2 texSize;
    OutTransmittanceLUT.GetDimensions(texSize.x, texSize.y);
    if (any(tID >= texSize))
        return;

    StructuredBuffer< SceneGlobals > sceneBuf = GetResource(g_Push.ID_Scene);
    const AtmosphereData atmosphere = sceneBuf[0].environment.atmosphere;

    const float2 uv = (float2(tID) + 0.5) / float2(texSize);

    float cosZenithAngle, viewHeight;
    GenerateTransmittanceCoordsFromUV(uv, atmosphere.planetRadiusKm, atmosphere.atmosphereRadiusKm, cosZenithAngle, viewHeight);

    const float3 rayOrigin = float3(0.0, viewHeight, 0.0);
    const float3 rayDir    = float3(safeSqrt(1.0 - cosZenithAngle * cosZenithAngle), cosZenithAngle, 0.0);

    const float2 groundIntersection     = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, atmosphere.planetRadiusKm);
    const float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, atmosphere.atmosphereRadiusKm);

    const float rayLength = groundIntersection.x > 0.0 ? groundIntersection.x : atmosphereIntersection.y;
    if (rayLength <= 0.0)
    {
        OutTransmittanceLUT[tID] = float3(0.0);
        return;
    }

    const float3 opticalDepth = ComputeOpticalDepth(atmosphere, rayOrigin, rayDir, rayLength, kNumOpticalDepthSamples);
    OutTransmittanceLUT[tID] = exp(-opticalDepth);
}
