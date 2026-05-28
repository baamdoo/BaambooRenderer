#version 460
#extension GL_GOOGLE_include_directive : require

#include "TerrainCommon.hg"

layout(location = 0) in vec2  inUv;
layout(location = 1) in float inSkirtBlend;
layout(location = 2) flat in uint inPatchDepth;

layout(location = 0) out vec4 outGBuffer0;
layout(location = 1) out vec4 outGBuffer1;
layout(location = 2) out vec3 outGBuffer2;
layout(location = 3) out vec4 outGBuffer3;

#define TERRAIN_MATERIAL_ID 255u

layout(set = 1, binding = 3) uniform sampler2D g_HeightmapPS;

vec3 ComputePixelHeightNormal(vec2 uv)
{
    vec2 texel = vec2(g_Terrain.HeightmapTexel);
    float hL = textureLod(g_HeightmapPS, uv - vec2(texel.x, 0.0), 0.0).r;
    float hR = textureLod(g_HeightmapPS, uv + vec2(texel.x, 0.0), 0.0).r;
    float hD = textureLod(g_HeightmapPS, uv - vec2(0.0, texel.y), 0.0).r;
    float hU = textureLod(g_HeightmapPS, uv + vec2(0.0, texel.y), 0.0).r;

    float dhdx = (hR - hL) * g_Terrain.HeightRangeMeter / (2.0 * g_Terrain.WorldPerTexel);
    float dhdz = (hU - hD) * g_Terrain.HeightRangeMeter / (2.0 * g_Terrain.WorldPerTexel);
    return normalize(vec3(-dhdx, 1.0, -dhdz));
}

void main()
{
    const vec3  surfaceAlbedo    = vec3(0.45, 0.38, 0.28);
    const float surfaceRoughness = 0.85;
    const vec3  cliffAlbedo      = vec3(0.30, 0.27, 0.22);
    const float cliffRoughness   = 0.95;

    const float s         = clamp(inSkirtBlend, 0.0, 1.0);
    const vec3  albedo    = mix(surfaceAlbedo,    cliffAlbedo,    s);
    const float roughness = mix(surfaceRoughness, cliffRoughness, s);
    const float ao        = 1.0;
    const float metallic  = 0.0;

    const vec3 N = ComputePixelHeightNormal(inUv);

    outGBuffer0 = vec4(albedo, ao);
    outGBuffer1 = vec4(N, float(TERRAIN_MATERIAL_ID) / 255.0);
    outGBuffer2 = vec3(0.0);
    outGBuffer3 = vec4(0.0, 0.0, roughness, metallic);
}
