#include "ShaderShared/Scene.h"
#include "Common/Atmosphere.slangh"

struct AtmosphereSkyboxPush
{
    uint ID_Scene;
    uint ID_TransmittanceLUT;
    uint ID_SkyViewLUT;
    uint ID_OutSkyboxLUT;
};

[[vk::push_constant]] ConstantBuffer< AtmosphereSkyboxPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

static const float kMaxSunDiskLuminance = 60000.0;

// Must match the face layout in StaticSkyboxCS
float3 GetRayDirectionFromCubemapCoord(uint3 tID, uint width, uint height)
{
    const float2 uv  = (float2(tID.xy) + 0.5) / float2(width, height);
    const float2 ndc = uv * 2.0 - 1.0;

    float3 rayDir = float3(0.0);
    switch (tID.z)
    {
    case 0: rayDir = float3(1.0, -ndc.y, -ndc.x);  break; // +X
    case 1: rayDir = float3(-1.0, -ndc.y, ndc.x);  break; // -X
    case 2: rayDir = float3(ndc.x, 1.0, ndc.y);    break; // +Y
    case 3: rayDir = float3(ndc.x, -1.0, -ndc.y);  break; // -Y
    case 4: rayDir = float3(ndc.x, -ndc.y, 1.0);   break; // +Z
    case 5: rayDir = float3(-ndc.x, -ndc.y, -1.0); break; // -Z
    }

    return normalize(rayDir);
}

// Must stay the inverse of GetSkyViewRayDirectionFromUV in AtmosphereSkyViewCS
float2 GetUvFromSkyViewRayDirection(float longitude, float latitude, float viewHeight, float planetRadiusKm, bool bIntersectGround)
{
    const float Vhorizon           = sqrt(viewHeight * viewHeight - planetRadiusKm * planetRadiusKm);
    const float cosBeta            = Vhorizon / viewHeight;
    const float beta               = acosFast4(cosBeta);
    const float zenithHorizonAngle = PI - beta;

    float2 uv;
    if (!bIntersectGround)
    {
        float coord = latitude / zenithHorizonAngle;
        coord = 1.0 - coord;
        coord = 1.0 - safeSqrt(coord);

        uv.y = 0.5 * coord;
    }
    else
    {
        float coord = (latitude - zenithHorizonAngle) / beta;
        coord = safeSqrt(coord);

        uv.y = 0.5 + 0.5 * coord;
    }

    uv.x = 0.5 * (longitude + PI) / PI;
    return uv;
}

// Disk luminance = illuminance / disk solid angle, attenuated along the sun direction
float3 GetSunDiskLuminance(AtmosphereData atmosphere, Texture2D< float3 > transmittanceLUT, float3 rayDir, float3 upVec, float viewHeight)
{
    const float3 sunDir    = -atmosphere.light.direction;
    const float  halfAngle = max(atmosphere.light.angularRadiusRad, 1e-4);
    if (dot(rayDir, sunDir) <= cos(halfAngle))
        return float3(0.0);

    const float  solidAngle    = 2.0 * PI * (1.0 - cos(halfAngle));
    const float3 transmittance = SampleTransmittanceLUT(transmittanceLUT, viewHeight, dot(upVec, sunDir), atmosphere.planetRadiusKm, atmosphere.atmosphereRadiusKm);
    return GetAtmosphereSunIlluminance(atmosphere.light) / solidAngle * transmittance;
}

[numthreads(8, 8, 6)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2DArray< float3 > OutSkyboxLUT = GetResource(g_Push.ID_OutSkyboxLUT);

    uint width, height, numFaces;
    OutSkyboxLUT.GetDimensions(width, height, numFaces);
    if (tID.x >= width || tID.y >= height)
        return;

    StructuredBuffer< SceneGlobals > sceneBuf = GetResource(g_Push.ID_Scene);
    const AtmosphereData atmosphere = sceneBuf[0].environment.atmosphere;

    Texture2D< float3 > TransmittanceLUT = GetResource(g_Push.ID_TransmittanceLUT);
    Texture2D< float3 > SkyViewLUT       = GetResource(g_Push.ID_SkyViewLUT);

    uint2 skyViewSize;
    SkyViewLUT.GetDimensions(skyViewSize.x, skyViewSize.y);

    const float3 rayOrigin  = GetViewPositionAbovePlanet(sceneBuf[0].camera.position, atmosphere.planetRadiusKm);
    const float  viewHeight = length(rayOrigin);
    const float3 rayDir     = GetRayDirectionFromCubemapCoord(tID, width, height);

    const float3 upVec       = rayOrigin / viewHeight;
    const float  cosLatitude = dot(rayDir, upVec);
    const float  longitude   = atan2Fast(rayDir.z, rayDir.x);

    const float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, atmosphere.planetRadiusKm);
    const bool   bIntersectGround   = groundIntersection.x > 0.0;

    float2 skyUV = GetUvFromSkyViewRayDirection(longitude, acosFast4(cosLatitude), viewHeight, atmosphere.planetRadiusKm, bIntersectGround);
    skyUV = GetUnstretchedTextureUV(skyUV, float2(skyViewSize));
    const float3 skyColor = SkyViewLUT.SampleLevel(g_LinearClampSampler, skyUV, 0);

    float3 disk = float3(0.0);
    if (!bIntersectGround)
    {
        disk = GetSunDiskLuminance(atmosphere, TransmittanceLUT, rayDir, upVec, viewHeight);

        // Hue-preserving cap: physical disk luminance far exceeds the RG11B10 range
        const float peak = max(disk.r, max(disk.g, disk.b));
        if (peak > kMaxSunDiskLuminance)
            disk *= kMaxSunDiskLuminance / peak;
    }

    OutSkyboxLUT[tID] = skyColor + disk;
}
