#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "Common.hg"

layout(set = SET_STATIC, binding = 0) uniform  sampler2D g_SceneTextures[];
layout(set = SET_STATIC, binding = 4) readonly buffer    MaterialBuffer 
{
    MaterialData materials[];
} g_MaterialBuffer;

layout(location = 0) in vec3 inPosWORLD;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inNormalWORLD;
layout(location = 3) in vec3 inTangentWORLD;
layout(location = 4) in flat uint inMaterialID;
layout(location = 5) in vec4 inPosCLIP_prev;
layout(location = 6) in vec4 inPosCLIP_curr;

layout(location = 0) out vec4 outGBuffer0; // albedo.rgb + AO.a
layout(location = 1) out vec4 outGBuffer1; // Normal.xyz + MaterialID.w
layout(location = 2) out vec4 outGBuffer2; // Emissive.rgb
layout(location = 3) out vec4 outGBuffer3; // MotionVectors.xy + Roughness.z + Metallic.w

//** Deprecated **//
// Octahedron normal encoding [Reference : https://www.shadertoy.com/view/cljGD1]
// vec2 OctEncode(vec3 n)
// {
//     n /= (abs(n.x) + abs(n.y) + abs(n.z));
// 
//     vec2 octWrap = 1.0 - abs( n.yx );
//     if (n.x < 0.0) octWrap.x = -octWrap.x;
//     if (n.y < 0.0) octWrap.y = -octWrap.y;
// 
//     n.xy = n.z >= 0.0 ? n.xy : octWrap;
//     return n.xy * 0.5 + 0.5;
// }

void main() 
{
    MaterialData material = g_MaterialBuffer.materials[inMaterialID];
    
    vec3 albedo = vec3(1.0);
    if (material.albedoID != INVALID_INDEX)
    {
        albedo = texture(g_SceneTextures[nonuniformEXT(material.albedoID)], inUv).rgb;
    }
    albedo *= material.tint;
    
    float metallic  = material.metallic;
    float roughness = material.roughness;
    float ao        = 1.0;
    if (material.metallicRoughnessAoID != INVALID_INDEX)
    {
        vec3 orm = texture(g_SceneTextures[nonuniformEXT(material.metallicRoughnessAoID)], inUv).rgb;

        ao        *= orm.r;
        metallic  *= orm.b;
        roughness *= orm.g;
    }
    
    // Calculate normal
    vec3 N = normalize(inNormalWORLD);
    if (material.normalID != INVALID_INDEX)
    {
        vec3 tangentNormal = texture(g_SceneTextures[nonuniformEXT(material.normalID)], inUv).xyz * 2.0 - 1.0;

        vec3 T   = normalize(inTangentWORLD);
        vec3 B   = cross(N, T);
        mat3 TBN = mat3(T, B, N);

        N = normalize(TBN * tangentNormal);
    }

    vec3 emissive = vec3(0.0);
    if (material.emissiveID != INVALID_INDEX)
    {
        emissive  = texture(g_SceneTextures[nonuniformEXT(material.emissiveID)], inUv).rgb;
        emissive *= material.emissivePower;
    }
    
    outGBuffer0 = vec4(albedo, ao);
    outGBuffer1 = vec4(N, float(inMaterialID) / 255.0);
    outGBuffer2 = vec4(emissive, 1.0);
    
    vec2 posPrevSCREEN = (inPosCLIP_prev.xy / inPosCLIP_prev.w) * 0.5 + 0.5;
    vec2 posCurrSCREEN = (inPosCLIP_curr.xy / inPosCLIP_curr.w) * 0.5 + 0.5;
    outGBuffer3        = vec4(posCurrSCREEN - posPrevSCREEN, roughness, metallic);
}