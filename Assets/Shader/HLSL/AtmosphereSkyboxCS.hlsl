#define _CAMERA
#define _SCENEENVIRONMENT
#include "AtmosphereCommon.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_SkyViewLUT   : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutSkyboxLUT : register(b2, ROOT_CONSTANT_SPACE);


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

float2 GetUvFromSkyViewRayDirection(float longitude, float latitude, float viewHeight, float planetRadiusKm, bool bIntersectGround)
{
    float2 uv;

    float Vhorizon = sqrt(viewHeight * viewHeight - planetRadiusKm * planetRadiusKm);
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

float3 GetSunLuminance(float3 rayDir, float3 L, bool bIntersectGround)
{
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
    Texture2D< float4 >        SkyViewLUT   = GetResource(g_SkyViewLUT.index);
    RWTexture2DArray< float3 > OutSkyboxLUT = GetResource(g_OutSkyboxLUT.index);

    uint width, height, numFaces;
    OutSkyboxLUT.GetDimensions(width, height, numFaces);
    if (tID.x >= width || tID.y >= height)
        return;

    // sky view
    float2 texSize;
    SkyViewLUT.GetDimensions(texSize.x, texSize.y);

    AtmosphereData Atmosphere = GetAtmosphereData();

    float3 cameraPos =
        float3(g_Camera.posWORLD.x, max(g_Camera.posWORLD.y, MIN_VIEW_HEIGHT_ABOVE_GROUND), g_Camera.posWORLD.z);
    float3 cameraPosAbovePlanet =
        cameraPos * DISTANCE_SCALE + float3(0.0, Atmosphere.planetRadiusKm, 0.0);
    float viewHeight = length(cameraPosAbovePlanet);

    float3 rayDir    = GetRayDirectionFromCubemapCoord(tID, width, height);
    float3 rayOrigin = cameraPosAbovePlanet;

    float3 upVec       = normalize(rayOrigin);
    float  cosLatitude = dot(rayDir, upVec);
    float  longitude   = atan2Fast(rayDir.z, rayDir.x);

    float2 groundIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, Atmosphere.planetRadiusKm);
    bool   bIntersectGround   = groundIntersection.x > 0.0;

    float3 sunDirection = float3(-Atmosphere.light.dirX, -Atmosphere.light.dirY, -Atmosphere.light.dirZ);

    float2 skyUV = GetUvFromSkyViewRayDirection(longitude, acosFast4(cosLatitude), viewHeight, Atmosphere.planetRadiusKm, bIntersectGround);
           skyUV = GetUnstretchedTextureUV(skyUV, texSize);
    float4 skyColor = SkyViewLUT.SampleLevel(g_LinearClampSampler, skyUV, 0);
    float3 color    = skyColor.rgb + GetSunLuminance(rayDir, sunDirection, bIntersectGround) * skyColor.a;

    OutSkyboxLUT[tID] = color;
}