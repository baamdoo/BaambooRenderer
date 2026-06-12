#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_mesh_shader : require

#include "VisibilityBuffer.hg"

layout(location = 0) perprimitiveEXT flat in uint v_DrawID;

layout(location = 0) out uint outVBuf0;
layout(location = 1) out uint outVBuf1;
layout(location = 2) out vec2 outVelocity;


void main()
{
    outVBuf0    = VISID_TERRAIN | (v_DrawID & 0x00FFFFFFu);
    outVBuf1    = 0u;
    outVelocity = vec2(0.0);
}
