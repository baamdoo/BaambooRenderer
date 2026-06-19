#version 460
#extension GL_GOOGLE_include_directive : require

#define _CAMERA
#include "DescriptorCommon.hg"

struct DebugLineVertex
{
    vec3 position;
    float pad0;
    vec3 color;
    float alpha;
};

layout(set = 1, binding = 0) readonly buffer DebugLineBuffer
{
    DebugLineVertex vertices[];
} g_DebugLines;

layout(location = 0) out vec4 v_Color;

void main()
{
    DebugLineVertex vertex = g_DebugLines.vertices[gl_VertexIndex];
    gl_Position = g_Camera.mViewProj * vec4(vertex.position, 1.0);
    v_Color = vec4(vertex.color, vertex.alpha);
}
