#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_mesh_shader : require

#define _MATERIAL
#include "DescriptorCommon.hg"
#include "VisibilityBuffer.hg"

layout(location = 0) in vec4 inPosCLIP_curr;
layout(location = 1) in vec4 inPosCLIP_prev;

layout(location = 6) perprimitiveEXT flat in uint inVisID0;
layout(location = 7) perprimitiveEXT flat in uint inVisID1;

layout(location = 0) out uint outVBuf0;    // visibility surface ID
layout(location = 1) out uint outVBuf1;    // visibility primitive ID
layout(location = 2) out vec2 outVelocity; // currUV - prevUV (Screen-space motion vector)


void main()
{
    vec2 posPrevUV = (inPosCLIP_prev.xy / inPosCLIP_prev.w) * 0.5 + 0.5;
    vec2 posCurrUV = (inPosCLIP_curr.xy / inPosCLIP_curr.w) * 0.5 + 0.5;

    outVBuf0    = inVisID0;
    outVBuf1    = inVisID1;
    outVelocity = posCurrUV - posPrevUV;
}