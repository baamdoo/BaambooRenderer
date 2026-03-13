#define _CAMERA
#define _MESH
#define _TRANSFORM
#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_TransformIndex;
    uint g_MaterialIndex;

    uint g_VertexOffset;
    uint g_MeshletCount;
    uint g_MeshletOffset;
    uint g_MeshletVertexOffset;
    uint g_MeshletTriangleOffset;
};

static StructuredBuffer< Vertex >  Vertices         = GetResource(g_Vertices.index);
static StructuredBuffer< Meshlet > Meshlets         = GetResource(g_Meshlets.index);
static StructuredBuffer< uint >    MeshletVertices  = GetResource(g_MeshletVertices.index);
static StructuredBuffer< uint >    MeshletTriangles = GetResource(g_MeshletTriangles.index);

uint hash(uint a)
{
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

struct AmplificationPayload
{
    uint meshletIndices[32];
};

struct MSOutput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void main(
    uint3 Gid : SV_GroupID, 
    uint3 GTid : SV_GroupThreadID,
    in payload AmplificationPayload Payload,
    out vertices MSOutput vertices[64], 
    out indices uint3 triangles[126])
{
    StructuredBuffer< TransformData > Transforms = GetResource(g_Transforms.index);
    TransformData transform = Transforms[g_TransformIndex];

    uint mi = Payload.meshletIndices[Gid.x];
    uint ti = GTid.x;

    Meshlet meshlet = Meshlets[mi];
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    uint mhash = hash(mi);
    float3 color = float3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;

    for (uint i = ti; i < meshlet.vertexCount; i += 32)
    {
        uint vi = g_VertexOffset + MeshletVertices[g_MeshletVertexOffset + meshlet.vertexOffset + i];

        Vertex vertex = Vertices[vi];

        float3 position = float3(vertex.posX, vertex.posY, vertex.posZ);
        float4 posWORLD = mul(transform.mLocalToWorld, float4(position, 1.0));
        // float3 normal   = float3(vertex.normalX, vertex.normalY, vertex.normalZ);

        vertices[i].position = mul(g_Camera.mViewProj, posWORLD);
        vertices[i].color    = float4(color, 1.0);
    }

    uint baseTriByteOffset = g_MeshletTriangleOffset + meshlet.triangleOffset;
    for (uint i = ti; i < meshlet.triangleCount; i += 32)
    {
        uint tPacked3 = MeshletTriangles[baseTriByteOffset + i];

        uint t0 = tPacked3 & 0xFF;
        uint t1 = (tPacked3 >> 8) & 0xFF;
        uint t2 = (tPacked3 >> 16) & 0xFF;

        triangles[i] = uint3(t0, t1, t2);
    }
}