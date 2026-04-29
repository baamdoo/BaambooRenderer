#define _CAMERA
#define _MESH
#define _TRANSFORM
#include "Common.hlsli"

#define CULL_FLAG_BACKFACE 1u
#define CULL_FLAG_SUBPIXEL 2u

cbuffer CommandSignatureParam : register(b0, COMMMANDSIGNATURE_SPACE)
{
    uint g_DrawID;
};

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float2 g_Viewport;

    uint g_CullFlags;
    uint g_Phase;
    uint g_HiZWidth;
    uint g_HiZHeight;
};

static StructuredBuffer< Vertex >  Vertices         = GetResource(g_Vertices.index);
static StructuredBuffer< Meshlet > Meshlets         = GetResource(g_Meshlets.index);
static StructuredBuffer< uint >    MeshletVertices  = GetResource(g_MeshletVertices.index);
static StructuredBuffer< uint >    MeshletTriangles = GetResource(g_MeshletTriangles.index);

static StructuredBuffer< MeshData >      Meshes     = GetResource(g_Meshes.index);
static StructuredBuffer< InstanceData >  Instances  = GetResource(g_Instances.index);
static StructuredBuffer< TransformData > Transforms = GetResource(g_Transforms.index);

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
    uint lod;
    uint meshletIndices[32];
};

struct MSOutput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

// Per-primitive cull marker (mirrors GLSL gl_CullPrimitiveEXT).
struct MSPrimitive
{
    bool cullPrimitive : SV_CullPrimitive;
};

// =========================================================================
// TriangleCull : per-primitive culling
//
// Behind-eye gate: if any vertex has clip.w <= 0 (behind near plane),
// don't cull — let HW handle the partial triangle culling correctly,
// since perspective division below near produces nonsense NDC coords.
// =========================================================================
bool TriangleCull(float4 ca, float4 cb, float4 cc)
{
    // Behind-eye gate
    if (ca.w <= 0.0 || cb.w <= 0.0 || cc.w <= 0.0)
        return false;

    // Perspective divide → NDC xy
    float2 a = ca.xy / ca.w;
    float2 b = cb.xy / cb.w;
    float2 c = cc.xy / cc.w;

    bool culled = false;

    if ((g_CullFlags & CULL_FLAG_BACKFACE) != 0u)
    {
        // Backface + zero-area via 2D signed area
        float2 eb = b - a;
        float2 ec = c - a;
        float  area = eb.y * ec.x - eb.x * ec.y;
        if (area <= 0.0)
            culled = true;
    }

    if ((g_CullFlags & CULL_FLAG_SUBPIXEL) != 0u && !culled)
    {
        // NDC [-1,1] → pixel coords [0, viewportSize]
        float2 sa = (a * 0.5 + 0.5) * g_Viewport;
        float2 sb = (b * 0.5 + 0.5) * g_Viewport;
        float2 sc = (c * 0.5 + 0.5) * g_Viewport;
        float2 bmin = min(sa, min(sb, sc));
        float2 bmax = max(sa, max(sb, sc));
        // Sub-pixel snap precision: 1/256 (D3D/Vulkan typical)
        const float subPixelPrec = 1.0 / 256.0;
        if (round(bmin.x - subPixelPrec) == round(bmax.x) ||
            round(bmin.y - subPixelPrec) == round(bmax.y))
            culled = true;
    }

    return culled;
}

groupshared float4 s_ClipPos[64];

[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void main(
    uint3 Gid : SV_GroupID,
    uint3 GTid : SV_GroupThreadID,
    in payload AmplificationPayload Payload,
    out vertices   MSOutput    vertices  [64],
    out indices    uint3       triangles [126],
    out primitives MSPrimitive prims     [126])
{
    InstanceData  instance  = Instances[g_DrawID];
    TransformData transform = Transforms[instance.transformID];
    MeshData      mesh      = Meshes[instance.meshID];

	uint mi = Payload.meshletIndices[Gid.x]; // mesh.mOffset is already baked into the meshlet indices by TaskShader
    uint ti = GTid.x;

    Meshlet meshlet = Meshlets[mi];
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    uint mhash = hash(Payload.lod);
    float3 color = float3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;

    for (uint i = ti; i < meshlet.vertexCount; i += 32)
    {
        uint vi = mesh.vOffset + MeshletVertices[mesh.lods[Payload.lod].mvOffset + meshlet.vertexOffset + i];

        Vertex vertex = Vertices[vi];

        float3 position = float3(vertex.posX, vertex.posY, vertex.posZ);
        float4 posWORLD = mul(transform.mLocalToWorld, float4(position, 1.0));
        // float3 normal   = float3(vertex.normalX, vertex.normalY, vertex.normalZ);

        float4 clipPos = mul(g_Camera.mViewProj, posWORLD);

        s_ClipPos[i] = clipPos;

        vertices[i].position = clipPos;
        vertices[i].color    = float4(color, 1.0);
    }

    // Per-vertex outputs(s_ClipPos[i]) are written by different invocations.
    GroupMemoryBarrierWithGroupSync();

    uint baseTriByteOffset = mesh.lods[Payload.lod].mtOffset + meshlet.triangleOffset;
    for (uint i = ti; i < meshlet.triangleCount; i += 32)
    {
        uint tPacked3 = MeshletTriangles[baseTriByteOffset + i];

        uint t0 = tPacked3 & 0xFF;
        uint t1 = (tPacked3 >> 8) & 0xFF;
        uint t2 = (tPacked3 >> 16) & 0xFF;

        triangles[i] = uint3(t0, t1, t2);

        float4 ca = s_ClipPos[t0];
        float4 cb = s_ClipPos[t1];
        float4 cc = s_ClipPos[t2];
        prims[i].cullPrimitive = TriangleCull(ca, cb, cc);
    }
}