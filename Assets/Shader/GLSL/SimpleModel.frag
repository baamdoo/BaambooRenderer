#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable
#include "../Common.bsh"

layout(set = SET_STATIC, binding = 0) uniform sampler2D g_SceneTextures[];
layout(set = SET_STATIC, binding = 5) readonly buffer   MaterialBuffer 
{
	MaterialData materials[];
} g_MaterialBuffer;

layout(location = 0) in vec3 inPosWORLD;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inNormalWORLD;
layout(location = 3) in vec3 inTangentWORLD;
layout(location = 4) in flat uint inMaterialID;

layout(location = 0) out vec4 outColor;

void main() 
{
	MaterialData material = g_MaterialBuffer.materials[inMaterialID];
    outColor = vec4(pow(texture(g_SceneTextures[nonuniformEXT(material.albedoID)], inUv).xyz, vec3(2.2)), 1.0);
}