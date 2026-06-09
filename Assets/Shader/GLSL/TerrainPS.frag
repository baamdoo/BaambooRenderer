#version 460
#extension GL_GOOGLE_include_directive : require

#include "VisibilityBuffer.hg"

layout(location = 0) out uint outVBuf0;
layout(location = 1) out uint outVBuf1;
layout(location = 2) out vec2 outVelocity;


void main()
{
    outVBuf0    = VISID_TERRAIN;
    outVBuf1    = 0u;
    outVelocity = vec2(0.0);
}
