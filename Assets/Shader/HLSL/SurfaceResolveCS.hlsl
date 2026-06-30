#define _CAMERA
#define _MESH
#define _TRANSFORM
#define _MATERIAL
#include "Common.hlsli"
#include "SurfaceResolve.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float2 g_Viewport;
};

ConstantBuffer< DescriptorHeapIndex > g_VBuf0          : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VBuf1          : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CoreNormal     : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CoreMaterial   : register(b4, ROOT_CONSTANT_SPACE);

ConstantBuffer< VoxelChunkDesc >      g_VoxelChunkDesc        : register(b0, space1);
ConstantBuffer< DescriptorHeapIndex > g_VoxelVertices         : register(b6, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshlets         : register(b7, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshletVertices  : register(b8, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshletTriangles : register(b9, ROOT_CONSTANT_SPACE);


ResolvedSurface ResolveVoxelSurface(uint v0, uint v1, float2 pixelCenter, float2 viewport)
{
    StructuredBuffer< Vertex >  Vertices         = GetResource(g_VoxelVertices.index);
    StructuredBuffer< Meshlet > Meshlets         = GetResource(g_VoxelMeshlets.index);
    StructuredBuffer< uint >    MeshletVertices  = GetResource(g_VoxelMeshletVertices.index);
    StructuredBuffer< uint >    MeshletTriangles = GetResource(g_VoxelMeshletTriangles.index);

    uint meshletIdx = VisMeshletIndex(v1); // absolute voxel meshlet-pool index (task shader baked the offset, matching the mesh path)
    uint triLocal   = VisTriLocal(v1);

    VoxelChunkDesc chunk   = g_VoxelChunkDesc; // TODO: multi-chunk voxels
    Meshlet        meshlet = Meshlets[meshletIdx];

    uint tPacked3  = MeshletTriangles[chunk.meshletTriangleOffset + meshlet.triangleOffset + triLocal];
    uint locals[3] = { tPacked3 & 0xFF, (tPacked3 >> 8) & 0xFF, (tPacked3 >> 16) & 0xFF };

    float3 originWS = float3(chunk.originX, chunk.originY, chunk.originZ);

    float3 posWS[3];
    float3 nrm[3];
    [unroll] for (uint k = 0; k < 3; ++k)
    {
        uint vi = chunk.vertexOffset + MeshletVertices[chunk.meshletVertexOffset + meshlet.vertexOffset + locals[k]];

        Vertex vv = Vertices[vi];
        posWS[k] = originWS + float3(vv.posX, vv.posY, vv.posZ);
        nrm[k]   = float3(vv.normalX, vv.normalY, vv.normalZ);
    }

    float4 c[3];
    [unroll] for (uint k = 0; k < 3; ++k)
        c[k] = mul(g_Camera.mViewProj, float4(posWS[k], 1.0));

    float2 ndc = (pixelCenter / viewport) * 2.0 - 1.0;
    ndc.y = -ndc.y; // NDC y-up vs pixel y-down

    float3 bary = Barycentrics(ndc, c[0], c[1], c[2]);
    float3 N    = normalize(bary.x * nrm[0] + bary.y * nrm[1] + bary.z * nrm[2]);

    ResolvedSurface rs;
    rs.matClass  = MATCLASS_STANDARD;
    rs.N         = N;
    rs.roughness = 0.9;
    rs.baseColor = float3(0.5, 0.5, 0.5);
    rs.metallic  = 0.0;
    return rs;
}


[numthreads(16, 16, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    uint2 px = tID.xy;
    if (px.x >= (uint)g_Viewport.x || px.y >= (uint)g_Viewport.y)
        return;

    RWTexture2D< float2 > CoreNormal   = GetResource(g_CoreNormal.index);
    RWTexture2D< float4 > CoreMaterial = GetResource(g_CoreMaterial.index);
    Texture2D< uint >     VBuf0        = GetResource(g_VBuf0.index);
    Texture2D< uint >     VBuf1        = GetResource(g_VBuf1.index);

    uint v0 = VBuf0.Load(int3(px, 0));

    if (VisIsSky(v0))
    {
        CoreNormal[px]   = float2(0.0, 0.0);
        CoreMaterial[px] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    uint   v1          = VBuf1.Load(int3(px, 0));
    float2 pixelCenter = float2(px) + 0.5;

    ResolvedSurface s;
    if (VisIsVoxel(v0))
        s = ResolveVoxelSurface(v0, v1, pixelCenter, g_Viewport);
    else
        s = ResolveMeshSurface(v0, v1, pixelCenter, g_Viewport);

    CoreNormal[px]   = OctEncode(s.N);
    CoreMaterial[px] = float4(s.roughness, (float)s.matClass / 255.0, 0.0, 0.0);
}
