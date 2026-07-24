#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_mesh_shader : require

#define _MATERIAL
#include "DescriptorCommon.hg"
#include "VisibilityBuffer.hg"

layout(location = 6) perprimitiveEXT flat in uint inVisID0;
layout(location = 7) perprimitiveEXT flat in uint inVisID1;

layout(location = 0) out uint outVBuf0;    // visibility surface ID
layout(location = 1) out uint outVBuf1;    // visibility primitive ID


void main()
{
    outVBuf0 = inVisID0;
    outVBuf1 = inVisID1;
}
