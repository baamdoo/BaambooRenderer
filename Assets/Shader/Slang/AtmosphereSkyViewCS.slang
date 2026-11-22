#include "ShaderShared/Scene.h"
#include "Common/Atmosphere.slangh"

struct AtmosphereSkyViewPush
{
    AtmosphereRaymarchConstants constants;

    uint ID_Scene;
    uint ID_TransmittanceLUT;
    uint ID_MultiScatteringLUT;
    uint ID_OutSkyViewLUT;
};

[[vk::push_constant]] ConstantBuffer< AtmosphereSkyViewPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Must stay the inverse of GetUvFromSkyViewRayDirection in AtmosphereSkyboxCS
float3 GetSkyViewRayDirectionFromUV(float2 uv, float viewHeight, float planetRadiusKm)
{
    const float Vhorizon           = sqrt(viewHeight * viewHeight - planetRadiusKm * planetRadiusKm);
    const float cosBeta            = Vhorizon / viewHeight;
    const float beta               = acosFast4(cosBeta);
    const float zenithHorizonAngle = PI - beta;

    float latitude;
    if (uv.y < 0.5)
    {
        float coord = 1.0 - 2.0 * uv.y;
        coord *= coord;
        coord  = 1.0 - coord;

        latitude = zenithHorizonAngle * coord;
    }
    else
    {
        float coord = uv.y * 2.0 - 1.0;
        coord *= coord;

        latitude = zenithHorizonAngle + beta * coord;
    }

    // +PI maps u = 0.5 to longitude 0 (+X), matching the atan2 range the reader uses
    const float longitude = uv.x * 2.0 * PI + PI;

    const float cosLatitude  = cos(latitude);
    const float sinLatitude  = sin(latitude);
    const float cosLongitude = cos(longitude);
    const float sinLongitude = sin(longitude);
    return float3(
        sinLatitude * cosLongitude,
        cosLatitude,
        sinLatitude * sinLongitude
    );
}

[numthreads(8, 8, 1)]
void main(uint2 tID : SV_DispatchThreadID)
{
    RWTexture2D< float3 > OutSkyViewLUT = GetResource(g_Push.ID_OutSkyViewLUT);

    uint2 texSize;
    OutSkyViewLUT.GetDimensions(texSize.x, texSize.y);
    if (any(tID >= texSize))
        return;

    StructuredBuffer< SceneGlobals > sceneBuf = GetResource(g_Push.ID_Scene);
    const AtmosphereData atmosphere = sceneBuf[0].environment.atmosphere;

    Texture2D< float3 > TransmittanceLUT   = GetResource(g_Push.ID_TransmittanceLUT);
    Texture2D< float3 > MultiScatteringLUT = GetResource(g_Push.ID_MultiScatteringLUT);

    const float2 uv = (float2(tID) + 0.5) / float2(texSize);

    const float  viewHeight = length(GetViewPositionAbovePlanet(sceneBuf[0].camera.position, atmosphere.planetRadiusKm));
    const float3 rayOrigin  = float3(0.0, viewHeight, 0.0);
    const float3 rayDir     = GetSkyViewRayDirectionFromUV(GetStretchedTextureUV(uv, float2(texSize)), viewHeight, atmosphere.planetRadiusKm);

    const float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, atmosphere.planetRadiusKm);
    const float  maxDistance        = groundIntersection.x > 0.0 ? groundIntersection.x : RAY_MARCHING_MAX_DISTANCE;

    float3 inScattered = float3(0.0);

    float rayStart, rayLength;
    if (GetAtmosphereRaySegment(atmosphere, rayOrigin, rayDir, maxDistance, rayStart, rayLength))
    {
        inScattered = IntegrateAtmosphereInScattering(
            atmosphere, TransmittanceLUT, MultiScatteringLUT,
            rayOrigin, rayDir, rayStart, rayLength, g_Push.constants.svMaxRaySteps).rgb;
    }

    OutSkyViewLUT[tID] = inScattered;
}
