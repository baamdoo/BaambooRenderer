#define _CAMERA
#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"

cbuffer CommandSignatureParam : register(b0, COMMMANDSIGNATURE_SPACE)
{
    uint g_DrawID; // chunk table index
};

ConstantBuffer< DescriptorHeapIndex > g_VoxelChunks           : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelVertices         : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshlets         : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshletVertices  : register(b4, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshletTriangles : register(b5, ROOT_CONSTANT_SPACE);


struct MSOutput
{
    float4 position  : SV_Position;
    float4 posCurrCS : POSITION0;
    float4 posPrevCS : POSITION1;
};

struct MSPrimitive
{
    nointerpolation uint visID0 : ID1;
    nointerpolation uint visID1 : ID2;
};

groupshared float3 sh_OriginWS;
groupshared uint   sh_VertexSlabBase;
groupshared uint   sh_MvBase;
groupshared uint   sh_MtBase;
groupshared uint   sh_VertexCount;
groupshared uint   sh_TriangleCount;

[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void main(
    uint3 Gid  : SV_GroupID,
    uint3 GTid : SV_GroupThreadID,
    out vertices   MSOutput    verts[64],
    out indices    uint3       tris[124],
    out primitives MSPrimitive primAttrs[124])
{
    uint localMeshletIndex = Gid.x;
    uint ti                = GTid.x;

    StructuredBuffer< VoxelChunk > Chunks   = GetResource(g_VoxelChunks.index);
    StructuredBuffer< Meshlet >    Meshlets = GetResource(g_VoxelMeshlets.index);

    if (ti == 0)
    {
        VoxelChunk chunk   = Chunks[g_DrawID];
        Meshlet    meshlet = Meshlets[chunk.meshletOffset + localMeshletIndex];

        sh_OriginWS       = float3(chunk.originX, chunk.originY, chunk.originZ);
        sh_VertexSlabBase = chunk.vertexOffset;
        sh_MvBase         = chunk.meshletVertexOffset + meshlet.vertexOffset;
        sh_MtBase         = chunk.meshletTriangleOffset + meshlet.triangleOffset;
        sh_VertexCount    = meshlet.vertexCount;
        sh_TriangleCount  = meshlet.triangleCount;
    }
    GroupMemoryBarrierWithGroupSync();

    SetMeshOutputCounts(sh_VertexCount, sh_TriangleCount);

    StructuredBuffer< Vertex > Vertices        = GetResource(g_VoxelVertices.index);
    StructuredBuffer< uint >   MeshletVertices = GetResource(g_VoxelMeshletVertices.index);

    for (uint i = ti; i < sh_VertexCount; i += 32)
    {
        uint vi = sh_VertexSlabBase + MeshletVertices[sh_MvBase + i];
        Vertex vertex = Vertices[vi];

        float3 posLS = float3(vertex.posX, vertex.posY, vertex.posZ);
        float4 posWS = float4(sh_OriginWS + posLS, 1.0);

        verts[i].position  = mul(g_Camera.mViewProj,               posWS);
        verts[i].posCurrCS = mul(g_Camera.mViewProjUnjittered,     posWS);
        verts[i].posPrevCS = mul(g_Camera.mViewProjUnjitteredPrev, posWS);
    }

    GroupMemoryBarrierWithGroupSync();

    StructuredBuffer< uint > MeshletTriangles = GetResource(g_VoxelMeshletTriangles.index);

    for (uint i = ti; i < sh_TriangleCount; i += 32)
    {
        uint tPacked3 = MeshletTriangles[sh_MtBase + i];
        uint t0 = tPacked3 & 0xFF;
        uint t1 = (tPacked3 >> 8) & 0xFF;
        uint t2 = (tPacked3 >> 16) & 0xFF;

        tris[i] = uint3(t0, t1, t2);

        primAttrs[i].visID0 = PackVisID0Voxel(g_DrawID);
        primAttrs[i].visID1 = PackVisID1(localMeshletIndex, i);
    }
}
