#version 460
#extension GL_GOOGLE_include_directive: require
#include "../Common.bsh"

layout(location = 0) out vec4 outColor;

void main() 
{
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}