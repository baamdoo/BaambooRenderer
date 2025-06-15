#version 460
#extension GL_GOOGLE_include_directive: require

#define _CAMERA
#include "Common.hg"

struct TransformData
{
	mat4 mWorldToView;
	mat4 mViewToWorld;
};

struct DrawData
{
	uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;

    uint materialID;
	uint transformID;
	uint transformCount;
};

layout(set = SET_STATIC, binding = 1) readonly buffer VertexBuffer 
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

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec2 outUv;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out flat uint outMaterialID;

void main() 
{
	DrawData drawData = g_DrawBuffer.draws[gl_DrawID];

	Vertex vertex           = g_VertexBuffer.vertices[gl_VertexIndex];
	TransformData transform = g_TransformBuffer.transforms[drawData.transformID];

	vec4 posWORLD     = transform.mWorldToView * vec4(vertex.posX, vertex.posY, vertex.posZ, 1.0);
	vec4 normalWORLD  = transform.mViewToWorld * vec4(vertex.normalX, vertex.normalY, vertex.normalZ, 1.0);
	vec4 tangentWORLD = transform.mWorldToView * vec4(vertex.tangentX, vertex.tangentY, vertex.tangentZ, 0.0);

	outPosition   = posWORLD.xyz;
	outUv         = vec2(vertex.u, vertex.v);
	outNormal     = normalize(normalWORLD.xyz);
	outTangent    = tangentWORLD.xyz;
	outMaterialID = drawData.materialID;

	gl_Position = g_Camera.mViewProj * posWORLD;
}