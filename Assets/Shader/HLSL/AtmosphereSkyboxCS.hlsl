#define _CAMERA
#include "Common.hlsli"
#include "AtmosphereCommon.hlsli"

Texture2D< float4 > g_SkyViewLUT : register(t0);

SamplerState g_LinearClampSampler : register(SAMPLER_INDEX_LINEAR_CLAMP);

RWTexture2DArray< float3 > g_SkyboxLUT : register(u0);

struct PushConstants
{
    float3 lightDir;
    float  planetRadius_km;
};
ConstantBuffer< PushConstants > g_Push : register(b0, ROOT_CONSTANT_SPACE);


float3 GetRayDirectionFromCubemapCoord(uint3 tID, uint width, uint height)
{
    float2 uv = (float2(tID.xy) + 0.5) / float2(width, height);
    float2 ndc = uv * 2.0 - 1.0;

    float3 rayDir;
    switch (tID.z)
    {
    case 0: rayDir = float3(1.0, -ndc.y, -ndc.x); break;  // +X
    case 1: rayDir = float3(-1.0, -ndc.y, ndc.x); break;  // -X
    case 2: rayDir = float3(ndc.x, 1.0, ndc.y); break;    // +Y
    case 3: rayDir = float3(ndc.x, -1.0, -ndc.y); break;  // -Y
    case 4: rayDir = float3(ndc.x, -ndc.y, 1.0); break;   // +Z
    case 5: rayDir = float3(-ndc.x, -ndc.y, -1.0); break; // -Z
    }

    return normalize(rayDir);
}

float2 GetUvFromSkyViewRayDirection(float longitude, float latitude, float viewHeight, bool bIntersectGround)
{
    float2 uv;

    float Vhorizon = sqrt(viewHeight * viewHeight - g_Push.planetRadius_km * g_Push.planetRadius_km);
    float cosBeta = Vhorizon / viewHeight;
    float beta = acosFast4(cosBeta);
    float zenithHorizonAngle = PI - beta;

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

    {
        uv.x = 0.5 * (longitude + PI) / PI;
    }

    return uv;
}

float3 GetSunLuminance(float3 rayDir, bool bIntersectGround)
{
    float3 L = float3(-g_Push.lightDir);
    if (dot(rayDir, L) > cos(0.5 * 0.505 * PI / 180.0))
    {
        if (!bIntersectGround)
        {
            const float3 SunLuminance = 1000000.0;
            return SunLuminance;
        }
    }
    return 0.0;
}


[numthreads(8, 8, 6)]
void main(uint3 tID : SV_DispatchThreadID)
{
    uint width, height, numFaces;
    g_SkyboxLUT.GetDimensions(width, height, numFaces);
    if (tID.x >= width || tID.y >= height)
        return;

    // sky view
    float2 texSize;
    g_SkyViewLUT.GetDimensions(texSize.x, texSize.y);

    float3 cameraPos =
        float3(g_Camera.posWORLD.x, max(g_Camera.posWORLD.y, MIN_VIEW_HEIGHT_ABOVE_GROUND), g_Camera.posWORLD.z);
    float3 cameraPosAbovePlanet =
        cameraPos * DISTANCE_SCALE + float3(0.0, g_Push.planetRadius_km, 0.0);
    float viewHeight = length(cameraPosAbovePlanet);

    float3 rayDir    = GetRayDirectionFromCubemapCoord(tID, width, height);
    float3 rayOrigin = cameraPosAbovePlanet;

    float3 upVec       = normalize(rayOrigin);
    float  cosLatitude = dot(rayDir, upVec);
    float  longitude   = atan2Fast(rayDir.z, rayDir.x);

    float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Push.planetRadius_km);
    bool   bIntersectGround   = groundIntersection.x > 0.0;

    float2 skyUV = GetUvFromSkyViewRayDirection(longitude, acosFast4(cosLatitude), viewHeight, bIntersectGround);
           skyUV = GetUnstretchedTextureUV(skyUV, texSize);
    float4 skyColor = g_SkyViewLUT.SampleLevel(g_LinearClampSampler, skyUV, 0);
    float3 color    = skyColor.rgb + GetSunLuminance(rayDir, bIntersectGround) * skyColor.a;

    g_SkyboxLUT[tID] = float4(color, 1.0);
}