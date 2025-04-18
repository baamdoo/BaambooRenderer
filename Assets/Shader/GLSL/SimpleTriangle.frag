#version 460
#extension GL_GOOGLE_include_directive: require
#include "../Common.bsh"

layout(location = 0) in vec4 inColor;
layout(location = 0) out vec4 outColor;

void main() 
{
    outColor = inColor;
}