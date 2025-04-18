#version 460
#extension GL_GOOGLE_include_directive: require
#include "../Common.bsh"

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

layout(set=1, binding=0) uniform TestColor
{
    vec4 color;
} Test;

layout(location = 0) out vec4 outColor;

void main() 
{
    outColor = Test.color;
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}