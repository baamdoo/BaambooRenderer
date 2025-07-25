#version 460
#extension GL_GOOGLE_include_directive: require

#define _CAMERA
#include "Common.hg"

layout(set = SET_STATIC, binding = 1, std430) readonly buffer VertexBuffer 
{
    Vertex vertices[];
} g_VertexBuffer;

layout(set = SET_STATIC, binding = 2) readonly buffer DrawBuffer 
{
    DrawData draws[];
} g_DrawBuffer;

layout(set = SET_STATIC, binding = 3) readonly buffer Transform 
{
    TransformData transforms[];
} g_TransformBuffer;

layout(location = 0) out vec3 outPosWORLD;
layout(location = 1) out vec2 outUv;
layout(location = 2) out vec3 outNormalWORLD;
layout(location = 3) out vec3 outTangentWORLD;
layout(location = 4) out flat uint outMaterialID;
layout(location = 5) out vec4 outPosCLIP_prev;
layout(location = 6) out vec4 outPosCLIP_curr;

void main() 
{
    DrawData drawData = g_DrawBuffer.draws[gl_DrawID];
    
    Vertex vertex           = g_VertexBuffer.vertices[gl_VertexIndex];
    TransformData transform = g_TransformBuffer.transforms[drawData.transformID];
    
    vec4 posWORLD     = transform.mWorldToView * vec4(vertex.pos, 1.0);
    vec4 normalWORLD  = transform.mWorldToView * vec4(vertex.normal, 0.0);
    vec4 tangentWORLD = transform.mWorldToView * vec4(vertex.tangent, 0.0);
    
    outPosWORLD     = posWORLD.xyz;
    outUv           = vertex.uv;
    outNormalWORLD  = normalize(normalWORLD.xyz);
    outTangentWORLD = normalize(tangentWORLD.xyz);
    outMaterialID   = drawData.materialID;
    
    vec4 posCLIP = g_Camera.mViewProj * posWORLD;
    gl_Position  = posCLIP;
    
    // TODO
    outPosCLIP_prev = posCLIP;
    outPosCLIP_curr = posCLIP;
}