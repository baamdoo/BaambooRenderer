#define _CAMERA
#define _FROZENCAMERA
#define _MESH
#define _TRANSFORM
#define _CULL
#define _VOXEL
#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"
#include "VoxelTerrainCommon.hlsli"

#define CULL_FLAG_BACKFACE 1u
#define CULL_FLAG_SUBPIXEL 2u

#define TEST_MODE 0

cbuffer CommandSignatureParam : register(b0, COMMMANDSIGNATURE_SPACE)
{
    uint g_DrawID;
};

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float2 g_Viewport;
    uint   g_Phase;
};

ConstantBuffer< VoxelChunkDesc >      g_VoxelChunkDesc   : register(b0, space1);
ConstantBuffer< DescriptorHeapIndex > g_ErosionDetailMap : register(b8, ROOT_CONSTANT_SPACE);

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
    uint diceMask;    // bit per payload slot: voxel meshlet inside the dicing radius (budget Lm > 0)
    uint slotCount;   // accepted meshlets this wave (prefix-scan upper bound)
    uint prefix[32];  // exclusive prefix of per-slot group counts (linear dispatch decode)
    uint lmPacked[4]; // per-slot budget level Lm, 4 bits each, 8 slots per word
    uint meshletIndices[32];
};

#if TEST_MODE == 1
struct MSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};
#else
struct MSOutput
{
    float4 position  : SV_Position;
    float4 posCurrCS : POSITION0;
    float4 posPrevCS : POSITION1;
};
#endif

struct MSPrimitive
{
    bool cullPrimitive : SV_CullPrimitive;

    nointerpolation uint visID0 : ID1; // (valid|lod|instanceID)
    nointerpolation uint visID1 : ID2; // (meshletIndex<<7)|triLocal
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

    if ((g_CullData.cullFlags & CULL_FLAG_BACKFACE) != 0u)
    {
        // Backface + zero-area via 2D signed area
        float2 eb = b - a;
        float2 ec = c - a;
        float  area = eb.y * ec.x - eb.x * ec.y;
        if (area <= 0.0)
            culled = true;
    }

    if ((g_CullData.cullFlags & CULL_FLAG_SUBPIXEL) != 0u && !culled)
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

// Lane 0 loads everything once, derived offsets are cached in shared memory to reduce LSGB stall which is the largest bottleneck in a frame.
groupshared float4x4 sh_LocalToWorld;
groupshared uint     sh_VOffset;
groupshared uint     sh_MvOffset;
groupshared uint     sh_MtOffset;
groupshared uint     sh_VertexCount;
groupshared uint     sh_TriangleCount;
groupshared uint     sh_VertexOffset;
groupshared uint     sh_TriangleOffset;
groupshared uint     sh_MaterialID;
groupshared uint     sh_IsVoxel;    // 1 = voxel chunk instance (geometry from voxel pools, voxel visID)
groupshared uint     sh_Lod;
groupshared uint     sh_VtxHeapIdx; // bindless heap index of the chosen vertex pool
groupshared uint     sh_MvHeapIdx;  // ... meshlet-vertex pool
groupshared uint     sh_MtHeapIdx;  // ... meshlet-triangle pool

groupshared float4 sh_ClipPos[64];

// Diced-path caches: max 10 base tris per group (budget L1) -> 30 corners.
groupshared float3 sh_CornerPos[30]; // chunk-local
groupshared float3 sh_CornerNrm[30];
groupshared uint   sh_TriLt[10];     // per-base-tri target level Lt = max(Le) (0 = plain pass-through)
groupshared uint   sh_TriLe[10];     // per-base-tri edge levels, packed: Le01 | Le12 << 4 | Le20 << 8
groupshared uint   sh_OutCounts[2];  // SetMeshOutputCounts args, published via groupshared

[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void main(
    uint3 Gid : SV_GroupID,
    uint3 GTid : SV_GroupThreadID,
    in payload AmplificationPayload Payload,
    out vertices   MSOutput    vertices[64],
    out indices    uint3       triangles[126],
    out primitives MSPrimitive primAttrs[126])
{
    // Compacted dispatch: recover (slot, localGroup) from the payload prefix
	// slot : the meshlet slot index (0..31) for this group
	// localGroup : which piece of the slot's meshlet this workgroup handles
    uint slot       = Gid.x;
    uint localGroup = 0u;
    if (Payload.diceMask != 0u)
    {
        slot = 0u;
        for (uint s = 1u; s < Payload.slotCount; ++s)
            if (Payload.prefix[s] <= Gid.x)
                slot = s;

        localGroup = Gid.x - Payload.prefix[slot];
    }

	uint mi = Payload.meshletIndices[slot]; // mesh.mOffset is already baked into the meshlet indices by TaskShader
    uint ti = GTid.x;

    uint slotLm = (Payload.lmPacked[slot >> 3u] >> ((slot & 7u) * 4u)) & 0xFu;
    bool diced  = (Payload.diceMask & (1u << slot)) != 0u;

    if (ti == 0)
    {
        // g_DrawID is the packed (lod << 24) | instanceID emitted by Instance Culling.
        uint instanceID = g_DrawID & 0x00FFFFFFu;

        InstanceData  instance  = Instances[instanceID];
        TransformData transform = Transforms[instance.transformID];
        MeshData      mesh      = Meshes[instance.meshID];

        StructuredBuffer< Meshlet > Meshlets = GetResource(instance.isVoxel ? g_VoxelMeshlets.index : g_MeshStreams.meshlets);
        Meshlet meshlet = Meshlets[mi];

        sh_LocalToWorld   = transform.mLocalToWorld;
        sh_VOffset        = mesh.vOffset;
        sh_MvOffset       = mesh.lods[Payload.lod].mvOffset;
        sh_MtOffset       = mesh.lods[Payload.lod].mtOffset;
        sh_VertexCount    = meshlet.vertexCount;
        sh_TriangleCount  = meshlet.triangleCount;
        sh_VertexOffset   = meshlet.vertexOffset;
        sh_TriangleOffset = meshlet.triangleOffset;
		sh_MaterialID     = instance.materialID;
        sh_IsVoxel        = instance.isVoxel;
        sh_Lod            = Payload.lod;
        sh_VtxHeapIdx     = instance.isVoxel ? g_VoxelVertices.index         : g_MeshStreams.vertices;
        sh_MvHeapIdx      = instance.isVoxel ? g_VoxelMeshletVertices.index  : g_MeshStreams.meshletVertices;
        sh_MtHeapIdx      = instance.isVoxel ? g_VoxelMeshletTriangles.index : g_MeshStreams.meshletTriangles;
    }
    GroupMemoryBarrierWithGroupSync();

    // num tris handled in this group
    uint T = 1u, child = 0u, triBase = 0u;
    if (diced)
    {
        if (slotLm <= 3u)
        {
            T       = kDiceTrisPerGroup[slotLm];
            triBase = localGroup * T;
        }
        else
        {
            uint numChildsPerTri = 1u << (2u * (slotLm - 3u));
            triBase = localGroup / numChildsPerTri;
            child   = localGroup % numChildsPerTri;
        }
    }
    uint numTris = (diced && triBase < sh_TriangleCount) ? min(T, sh_TriangleCount - triBase) : 0u;

    if (diced && numTris != 0u)
    {
        StructuredBuffer< Vertex > DVertices        = GetResource(sh_VtxHeapIdx);
        StructuredBuffer< uint >   DMeshletVertices = GetResource(sh_MvHeapIdx);
        for (uint c = ti; c < numTris * 3u; c += 32)
        {
            // base tri t's corner c sits at meshlet-local slot t*3+c
            uint local = (triBase + c / 3u) * 3u + (c % 3u);
            uint vi    = sh_VOffset + DMeshletVertices[sh_MvOffset + sh_VertexOffset + local];

            Vertex vv = DVertices[vi];
            sh_CornerPos[c] = float3(vv.posX, vv.posY, vv.posZ);
            sh_CornerNrm[c] = float3(vv.normalX, vv.normalY, vv.normalZ);
        }
        GroupMemoryBarrierWithGroupSync();

        // camera position in chunk-local space
        float3 cameraPos = g_FrozenCamera.posWORLD - float3(g_VoxelChunkDesc.originX, g_VoxelChunkDesc.originY, g_VoxelChunkDesc.originZ);
        for (uint t = ti; t < numTris; t += 32)
        {
            uint b   = t * 3u;
            uint le0 = DiceEdgeLevel(sh_CornerPos[b + 0u], sh_CornerPos[b + 1u], cameraPos, g_VoxelChunkDesc);
            uint le1 = DiceEdgeLevel(sh_CornerPos[b + 1u], sh_CornerPos[b + 2u], cameraPos, g_VoxelChunkDesc);
            uint le2 = DiceEdgeLevel(sh_CornerPos[b + 2u], sh_CornerPos[b + 0u], cameraPos, g_VoxelChunkDesc);

            sh_TriLe[t] = le0 | (le1 << 4u) | (le2 << 8u);
            sh_TriLt[t] = max3(le0, le1, le2);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Output counts: emits based on highest level among 3-edges. Flat regime sums per-tri sizes; hier regime emits one level-3 child.
    bool hier = false;
    uint outVerts = 0u, outPrims = 0u;
    if (!diced)
    {
        outVerts = sh_VertexCount;
        outPrims = sh_TriangleCount;
    }
    else if (numTris != 0u)
    {
        uint Lt0 = sh_TriLt[0];
        hier = (slotLm >= 4u) && (Lt0 >= 4u);
        if (hier)
        {
			// early-return if this group is extra padding dispatched by conservative level calculation
            bool active = child < (1u << (2u * (Lt0 - 3u)));
            outVerts = active ? 45u : 0u; // DiceSubVertCount(3)
            outPrims = active ? 64u : 0u; // DiceSubTriCount(3)
        }
        else if (child == 0u)
        {
            for (uint t = 0u; t < numTris; ++t)
            {
                uint lt = sh_TriLt[t];
                outVerts += (lt == 0u) ? 3u : DiceSubVertCount(lt);
                outPrims += (lt == 0u) ? 1u : DiceSubTriCount(lt);
            }
            outVerts = min(outVerts, 64u);
            outPrims = min(outPrims, 124u);
        }
    }

    if (ti == 0u)
    {
        sh_OutCounts[0] = outVerts;
        sh_OutCounts[1] = outPrims;
    }
    GroupMemoryBarrierWithGroupSync();
    outVerts = sh_OutCounts[0];
    outPrims = sh_OutCounts[1];

    SetMeshOutputCounts(outVerts, outPrims);
    if (outVerts == 0u)
        return;

    if (diced)
    {
        Texture2D< float4 > ErosionMap = GetResource(g_ErosionDetailMap.index);

        if (hier)
        {
            // Handle child diced internally at level 3(FIXED); VBuf1 sub-index = child*64 + local.
            uint  Lt = sh_TriLt[0];
            uint3 le = uint3(sh_TriLe[0] & 0xFu, (sh_TriLe[0] >> 4u) & 0xFu, (sh_TriLe[0] >> 8u) & 0xFu);

            uint3 cc0, cc1, cc2;
            DiceChildCorners(child, Lt, cc0, cc1, cc2);

            for (uint v = ti; v < 45u; v += 32)
            {
                uint3 coord = DiceHierCoord(cc0, cc1, cc2, DiceSubVertexCoordInt(v, 3u));

                float3 posL, nrmL;
                DiceSubVertex(coord, Lt, le,
                              sh_CornerPos[0], sh_CornerPos[1], sh_CornerPos[2],
                              sh_CornerNrm[0], sh_CornerNrm[1], sh_CornerNrm[2],
                              posL, nrmL);

                float3 posWS = mul(sh_LocalToWorld, float4(posL, 1.0)).xyz;
                posWS = DisplaceVoxelDice(posWS, nrmL.y, g_FrozenCamera.posWORLD,
                                          g_VoxelChunkDesc, ErosionMap, g_LinearClampSampler);

                vertices[v].position = mul(g_Camera.mViewProj, float4(posWS, 1.0));
#if TEST_MODE == 1
                vertices[v].color    = float4(normalize(nrmL) * 0.5 + 0.5, 1.0);
#else
                vertices[v].posCurrCS = mul(g_Camera.mViewProjUnjittered,     float4(posWS, 1.0));
                vertices[v].posPrevCS = mul(g_Camera.mViewProjUnjitteredPrev, float4(posWS, 1.0));
#endif
            }

            for (uint p = ti; p < 64u; p += 32)
            {
                triangles[p] = DiceSubTriVerts(p, 3u); // local indices into the 45-vert set

                primAttrs[p].cullPrimitive = false;
                primAttrs[p].visID0 = PackVisID0Voxel(g_DrawID & 0x00FFFFFFu);
                primAttrs[p].visID1 = PackVisID1Voxel(mi, triBase, child * 64u + p + 1u);
            }

            return;
        }

        for (uint v = ti; v < outVerts; v += 32)
        {
            // locate the owning base tri by walking the variable-size layout (numTris <= 10)
            uint t = 0u, vStart = 0u;
            for (;;)
            {
                uint lt2 = sh_TriLt[t];
                uint sz  = (lt2 == 0u) ? 3u : DiceSubVertCount(lt2);
                if (v < vStart + sz)
                    break;
                vStart += sz; ++t;
            }
            uint lt    = sh_TriLt[t];
            uint local = v - vStart;

            float3 pos, normal;
            if (lt != 0u)
            {
                uint3 le = uint3(sh_TriLe[t] & 0xFu, (sh_TriLe[t] >> 4u) & 0xFu, (sh_TriLe[t] >> 8u) & 0xFu);
                DiceSubVertex(DiceSubVertexCoordInt(local, lt), lt, le,
                              sh_CornerPos[t * 3u + 0u], sh_CornerPos[t * 3u + 1u], sh_CornerPos[t * 3u + 2u],
                              sh_CornerNrm[t * 3u + 0u], sh_CornerNrm[t * 3u + 1u], sh_CornerNrm[t * 3u + 2u], 
                              pos, normal);
            }
            else
            {
                pos    = sh_CornerPos[t * 3u + local];
                normal = sh_CornerNrm[t * 3u + local];
            }

            float3 posWS = mul(sh_LocalToWorld, float4(pos, 1.0)).xyz;
            if (lt != 0u)
                posWS = DisplaceVoxelDice(posWS, normal.y, g_FrozenCamera.posWORLD,
                                          g_VoxelChunkDesc, ErosionMap, g_LinearClampSampler);

            vertices[v].position = mul(g_Camera.mViewProj, float4(posWS, 1.0));
#if TEST_MODE == 1
            vertices[v].color    = float4(normalize(normal) * 0.5 + 0.5, 1.0);
#else
            vertices[v].posCurrCS = mul(g_Camera.mViewProjUnjittered,     float4(posWS, 1.0));
            vertices[v].posPrevCS = mul(g_Camera.mViewProjUnjitteredPrev, float4(posWS, 1.0));
#endif
        }

        for (uint p = ti; p < outPrims; p += 32)
        {
            // walk prim/vert layouts together to recover the tri's vertex base
            uint t = 0u, pStart = 0u, vStart = 0u;
            for (;;)
            {
                uint lt2 = sh_TriLt[t];
                uint psz = (lt2 == 0u) ? 1u : DiceSubTriCount(lt2);
                if (p < pStart + psz)
                    break;
                pStart += psz; vStart += (lt2 == 0u) ? 3u : DiceSubVertCount(lt2); ++t;
            }
            uint lt = sh_TriLt[t];

            if (lt != 0u)
            {
                uint  st  = p - pStart;
                uint3 sub = DiceSubTriVerts(st, lt);

                triangles[p] = vStart + sub;
                primAttrs[p].visID1 = PackVisID1Voxel(mi, triBase + t, st + 1u);
            }
            else
            {
                triangles[p] = uint3(vStart, vStart + 1u, vStart + 2u);
                primAttrs[p].visID1 = PackVisID1Voxel(mi, triBase + t, 0u); // sentinel 0 = undiced base triangle
            }
            primAttrs[p].cullPrimitive = false;
            primAttrs[p].visID0 = PackVisID0Voxel(g_DrawID & 0x00FFFFFFu);
        }

        return;
    }

    StructuredBuffer< Vertex > Vertices        = GetResource(sh_VtxHeapIdx);
    StructuredBuffer< uint >   MeshletVertices = GetResource(sh_MvHeapIdx);

    for (uint i = ti; i < sh_VertexCount; i += 32)
    {
        uint vi = sh_VOffset + MeshletVertices[sh_MvOffset + sh_VertexOffset + i];

        Vertex vertex = Vertices[vi];

        float3 position = float3(vertex.posX, vertex.posY, vertex.posZ);
        float4 posWS    = mul(sh_LocalToWorld, float4(position, 1.0));

        float4 posCS = mul(g_Camera.mViewProj, posWS);

        sh_ClipPos[i] = posCS;

        vertices[i].position = posCS;
#if TEST_MODE == 1
        vertices[i].color    = float4(float3(vertex.normalX, vertex.normalY, vertex.normalZ) * 0.5 + 0.5, 1.0);
#else
        vertices[i].posCurrCS = mul(g_Camera.mViewProjUnjittered, posWS);
        vertices[i].posPrevCS = mul(g_Camera.mViewProjUnjitteredPrev, posWS);
#endif
    }

    // Per-vertex outputs(s_ClipPos[i]) are written by different invocations.
    GroupMemoryBarrierWithGroupSync();

    StructuredBuffer< uint > MeshletTriangles = GetResource(sh_MtHeapIdx);
    uint baseTriByteOffset = sh_MtOffset + sh_TriangleOffset;
    for (uint i = ti; i < sh_TriangleCount; i += 32)
    {
        uint tPacked3 = MeshletTriangles[baseTriByteOffset + i];

        uint t0 = tPacked3 & 0xFF;
        uint t1 = (tPacked3 >> 8) & 0xFF;
        uint t2 = (tPacked3 >> 16) & 0xFF;

        triangles[i] = uint3(t0, t1, t2);

        float4 ca = sh_ClipPos[t0];
        float4 cb = sh_ClipPos[t1];
        float4 cc = sh_ClipPos[t2];

        // TODO: LOD streaming and culling on voxels
        primAttrs[i].cullPrimitive = (sh_IsVoxel != 0u) ? false : TriangleCull(ca, cb, cc);

        primAttrs[i].visID0 = (sh_IsVoxel != 0u)
            ? PackVisID0Voxel(g_DrawID & 0x00FFFFFFu)
            : PackVisID0Mesh(g_DrawID & 0x00FFFFFFu, sh_Lod, 0u);
        primAttrs[i].visID1 = (sh_IsVoxel != 0u)
            ? PackVisID1Voxel(mi, i, 0u) // sentinel 0 = undiced base triangle
            : PackVisID1(mi, i);
    }
}