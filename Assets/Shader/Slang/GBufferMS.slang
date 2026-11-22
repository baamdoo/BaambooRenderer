#include "ShaderShared/Scene.h"
#include "ShaderShared/Mesh.h"
#include "ShaderShared/VoxelTerrain.h"
#include "Common/GBuffer.slangh"
#include "Common/VisibilityBuffer.slangh"
#include "Common/VoxelTerrain.slangh"

struct MSOutput
{
    float4 position : SV_Position;
};

// D3D12 gives the pixel shader's globals COLOR0..2 in declaration order, so these semantics must match them
struct MSPrimitive
{
    bool cullPrimitive : SV_CullPrimitive;

    [[vk::location(0)]] nointerpolation uint visID0 : COLOR0;
    [[vk::location(1)]] nointerpolation uint visID1 : COLOR1;
    [[vk::location(2)]] nointerpolation uint fade   : COLOR2; // voxel chunk flags, 0 for meshes
};

bool TriangleCull(float4 ca, float4 cb, float4 cc, uint cullFlags)
{
    // Any vertex behind the eye: perspective division is meaningless, leave it to the rasterizer
    if (ca.w <= 0.0 || cb.w <= 0.0 || cc.w <= 0.0)
        return false;

    const float2 a = ca.xy / ca.w;
    const float2 b = cb.xy / cb.w;
    const float2 c = cc.xy / cc.w;

    bool culled = false;

    if ((cullFlags & kCullFlagTriangleBackface) != 0u)
    {
        const float2 eb   = b - a;
        const float2 ec   = c - a;
        const float  area = eb.y * ec.x - eb.x * ec.y;
        if (area <= 0.0)
            culled = true;
    }

    if ((cullFlags & kCullFlagTriangleSubpixel) != 0u && !culled)
    {
        const float2 viewport = g_Push.constants.viewport;
        const float2 sa = (a * 0.5 + 0.5) * viewport;
        const float2 sb = (b * 0.5 + 0.5) * viewport;
        const float2 sc = (c * 0.5 + 0.5) * viewport;
        const float2 bmin = min(sa, min(sb, sc));
        const float2 bmax = max(sa, max(sb, sc));

        // Sub-pixel snap precision 1/256
        const float subPixelPrec = 1.0 / 256.0;
        if (round(bmin.x - subPixelPrec) == round(bmax.x) || round(bmin.y - subPixelPrec) == round(bmax.y))
            culled = true;
    }

    return culled;
}

groupshared float4x4 sh_LocalToWorld;
groupshared uint     sh_VOffset;
groupshared uint     sh_MvOffset;
groupshared uint     sh_MtOffset;
groupshared uint     sh_VertexCount;
groupshared uint     sh_TriangleCount;
groupshared uint     sh_VertexOffset;
groupshared uint     sh_TriangleOffset;
groupshared uint     sh_VoxelChunkIndex;
groupshared uint     sh_VtxHeapIdx;
groupshared uint     sh_MvHeapIdx;
groupshared uint     sh_MtHeapIdx;

groupshared float4 sh_ClipPos[64];

// Diced path: at most 10 base triangles per group (budget level 1), 30 corners
groupshared float3 sh_CornerPosWS[30];
groupshared float3 sh_CornerNrm[30];
groupshared uint   sh_TriLt[10];    // per-base-triangle target level Lt = max(Le), 0 = pass-through
groupshared uint   sh_TriLe[10];    // per-base-triangle edge levels: Le01 | Le12 << 4 | Le20 << 8
groupshared uint   sh_OutCounts[2];

[shader("mesh")]
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
    StructuredBuffer< SceneGlobals > sceneBuf = GetResource(g_Push.ID_Scene);

    const uint laneBits  = Payload.tsLaneBits;
    const uint slotCount = countbits(laneBits);

    // Compacted dispatch: lane t owns slot t; a wave prefix sum over per-slot group counts recovers (slot, localGroup)
    const uint ti         = GTid.x;
    const uint tLm        = (Payload.LmPacked[ti >> 3u] >> ((ti % 8u) * 4u)) & 0xFu;
    const uint tTriCount  = (Payload.triCountPacked[ti >> 2u] >> ((ti % 4u) * 8u)) & 0xFFu;
    const uint tGroups    = DiceGroupsForMeshlet(max(tLm, 1u), tTriCount);
    const uint tNumGroups = (ti < slotCount) ? ((tLm > 0u) ? tGroups : 1u) : 0u;
    const uint tPrefix    = WavePrefixSum(tNumGroups);

    const bool slotMatch  = (Gid.x - tPrefix) < tNumGroups;
    const uint slot       = firstbitlow(WaveActiveBallot(slotMatch).x);
    const uint localGroup = Gid.x - WaveActiveSum(slotMatch ? tPrefix : 0u); // exactly one lane matches

    // The slot-th set bit of laneBits
    const bool tSet        = ((laneBits >> ti) & 1u) != 0u;
    const uint tSetPrefix  = WavePrefixCountBits(tSet);
    const bool laneMatch   = tSet ? (tSetPrefix == slot) : false;
    const uint tsLaneIndex = firstbitlow(WaveActiveBallot(laneMatch).x);

    const uint mi = Payload.miBaseGroupID + tsLaneIndex; // the task shader already added the LOD's meshlet offset

    const uint slotLm = (Payload.LmPacked[slot >> 3u] >> ((slot & 7u) * 4u)) & 0xFu;
    const bool diced  = (Payload.diceMask & (1u << slot)) != 0u;

    const uint instanceID = DrawInstanceID(Payload.drawID);
    const uint lod        = DrawLOD(Payload.drawID);

    if (ti == 0)
    {
        const SceneBufferIndices buffers = sceneBuf[0].buffers;

        StructuredBuffer< InstanceData >  instances  = GetResource(buffers.instances);
        StructuredBuffer< TransformData > transforms = GetResource(buffers.transforms);
        StructuredBuffer< MeshData >      meshes     = GetResource(buffers.meshes);

        const InstanceData  instance  = instances[instanceID];
        const TransformData transform = transforms[instance.transformID];
        const MeshData      mesh      = meshes[instance.meshID];
        const bool          bVoxel    = instance.isVoxel != 0u;

        StructuredBuffer< Meshlet > meshlets = GetResource(bVoxel ? g_Push.ID_VoxelMeshlets : buffers.meshlets);
        const Meshlet meshlet = meshlets[mi];

        sh_LocalToWorld    = transform.mLocalToWorld;
        sh_VOffset         = mesh.vOffset;
        sh_MvOffset        = mesh.lods[lod].mvOffset;
        sh_MtOffset        = mesh.lods[lod].mtOffset;
        sh_VertexCount     = meshlet.vertexCount;
        sh_TriangleCount   = meshlet.triangleCount;
        sh_VertexOffset    = meshlet.vertexOffset;
        sh_TriangleOffset  = meshlet.triangleOffset;
        sh_VoxelChunkIndex = bVoxel ? (instanceID - kVoxelChunkInstanceBase) : kInvalidIndex;
        sh_VtxHeapIdx      = bVoxel ? g_Push.ID_VoxelVertices         : buffers.vertices;
        sh_MvHeapIdx       = bVoxel ? g_Push.ID_VoxelMeshletVertices  : buffers.meshletVertices;
        sh_MtHeapIdx       = bVoxel ? g_Push.ID_VoxelMeshletTriangles : buffers.meshletTriangles;
    }
    GroupMemoryBarrierWithGroupSync();

    const bool   isVoxel     = sh_VoxelChunkIndex != kInvalidIndex;
    const float3 cameraPosWS = sceneBuf[0].frozenCamera.position;
    const float4x4 viewProj  = sceneBuf[0].camera.mViewProj;

    VoxelChunkDesc chunk = (VoxelChunkDesc)0;
    if (isVoxel)
    {
        StructuredBuffer< VoxelChunkDesc > chunks = GetResource(g_Push.ID_VoxelChunkDescs);
        chunk = chunks[sh_VoxelChunkIndex];
    }

    const uint chunkBaseMI = chunk.mOffset;

    // Base triangles handled by this group
    uint T = 1u, triBase = 0u;
    if (diced)
    {
        T       = kDiceTrisPerGroup[slotLm];
        triBase = localGroup * T;
    }
    const uint numTris = (diced && triBase < sh_TriangleCount) ? min(T, sh_TriangleCount - triBase) : 0u;

    if (diced && numTris != 0u)
    {
        StructuredBuffer< VoxelVertex > dVertices        = GetResource(sh_VtxHeapIdx);
        StructuredBuffer< uint >        dMeshletVertices = GetResource(sh_MvHeapIdx);
        StructuredBuffer< uint >        dMeshletTris     = GetResource(sh_MtHeapIdx);
        for (uint c = ti; c < numTris * 3u; c += 32)
        {
            // corner c of a base triangle -> its meshlet-local vertex, byte c % 3 of the packed triplet
            const uint local = (dMeshletTris[sh_MtOffset + sh_TriangleOffset + triBase + c / 3u] >> (8u * (c % 3u))) & 0xFFu;
            const uint vi    = sh_VOffset + dMeshletVertices[sh_MvOffset + sh_VertexOffset + local];

            const VoxelVertex vv       = dVertices[vi];
            const float3      cornerWS = VoxelUnpackPosTransition(vv, chunk.chunkSizeMeter, chunk.lodAndMask) + chunk.originWS;
            sh_CornerNrm[c]   = VoxelUnpackNormal(vv);
            sh_CornerPosWS[c] = VoxelMorphPosWS(vv, cornerWS, sh_CornerNrm[c], cameraPosWS, chunk);
        }
        GroupMemoryBarrierWithGroupSync();

        for (uint t = ti; t < numTris; t += 32)
        {
            const uint b   = t * 3u;
            const uint le0 = DiceEdgeLevel(sh_CornerPosWS[b + 0u], sh_CornerPosWS[b + 1u], cameraPosWS, chunk);
            const uint le1 = DiceEdgeLevel(sh_CornerPosWS[b + 1u], sh_CornerPosWS[b + 2u], cameraPosWS, chunk);
            const uint le2 = DiceEdgeLevel(sh_CornerPosWS[b + 2u], sh_CornerPosWS[b + 0u], cameraPosWS, chunk);

            sh_TriLe[t] = le0 | (le1 << 4u) | (le2 << 8u);
            sh_TriLt[t] = max(le0, max(le1, le2));
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Each base triangle emits the sub-mesh of its highest edge level
    uint outVerts = 0u, outPrims = 0u;
    if (!diced)
    {
        outVerts = sh_VertexCount;
        outPrims = sh_TriangleCount;
    }
    else
    {
        for (uint t = 0u; t < numTris; ++t)
        {
            const uint lt = sh_TriLt[t];
            outVerts += (lt == 0u) ? 3u : DiceSubVertCount(lt);
            outPrims += (lt == 0u) ? 1u : DiceSubTriCount(lt);
        }
        outVerts = min(outVerts, 64u);
        outPrims = min(outPrims, 124u);
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
        Texture2DArray< float4 > erosionMap = GetResource(g_Push.ID_ErosionDetailMap);

        for (uint v = ti; v < outVerts; v += 32)
        {
            // Find the owning base triangle by walking the variable-size layout (numTris <= 10)
            uint t = 0u, vStart = 0u;
            for (;;)
            {
                const uint lt2 = sh_TriLt[t];
                const uint sz  = (lt2 == 0u) ? 3u : DiceSubVertCount(lt2);
                if (v < vStart + sz)
                    break;
                vStart += sz;
                ++t;
            }
            const uint lt    = sh_TriLt[t];
            const uint local = v - vStart;

            float3 posWS, normal;
            if (lt != 0u)
            {
                const uint3 le = uint3(sh_TriLe[t] & 0xFu, (sh_TriLe[t] >> 4u) & 0xFu, (sh_TriLe[t] >> 8u) & 0xFu);
                DiceSubVertex(DiceSubVertexCoordInt(local, lt), lt, le,
                              sh_CornerPosWS[t * 3u + 0u], sh_CornerPosWS[t * 3u + 1u], sh_CornerPosWS[t * 3u + 2u],
                              sh_CornerNrm[t * 3u + 0u], sh_CornerNrm[t * 3u + 1u], sh_CornerNrm[t * 3u + 2u],
                              posWS, normal);
                posWS = DisplaceVoxelDice(posWS, normal.y, cameraPosWS, chunk, erosionMap, g_LinearClampSampler);
            }
            else
            {
                posWS  = sh_CornerPosWS[t * 3u + local];
                normal = sh_CornerNrm[t * 3u + local];
            }

            vertices[v].position = mul(viewProj, float4(posWS, 1.0));
        }

        for (uint p = ti; p < outPrims; p += 32)
        {
            // Walk the primitive and vertex layouts together to recover the triangle's vertex base
            uint t = 0u, pStart = 0u, vStart = 0u;
            for (;;)
            {
                const uint lt2 = sh_TriLt[t];
                const uint psz = (lt2 == 0u) ? 1u : DiceSubTriCount(lt2);
                if (p < pStart + psz)
                    break;
                pStart += psz;
                vStart += (lt2 == 0u) ? 3u : DiceSubVertCount(lt2);
                ++t;
            }
            const uint lt = sh_TriLt[t];

            if (lt != 0u)
            {
                const uint st = p - pStart;
                triangles[p]        = vStart + DiceSubTriVerts(st, lt);
                primAttrs[p].visID1 = PackVisID1Voxel(mi - chunkBaseMI, triBase + t, st + 1u);
            }
            else
            {
                triangles[p]        = uint3(vStart, vStart + 1u, vStart + 2u);
                primAttrs[p].visID1 = PackVisID1Voxel(mi - chunkBaseMI, triBase + t, 0u); // 0 = undiced base triangle
            }
            primAttrs[p].cullPrimitive = false;
            primAttrs[p].visID0        = PackVisID0Voxel(instanceID);
            primAttrs[p].fade          = chunk.flags;
        }

        return;
    }

    StructuredBuffer< Vertex >      meshVertices    = GetResource(sh_VtxHeapIdx);
    StructuredBuffer< VoxelVertex > voxelVertices   = GetResource(sh_VtxHeapIdx);
    StructuredBuffer< uint >        meshletVertices = GetResource(sh_MvHeapIdx);

    for (uint i = ti; i < sh_VertexCount; i += 32)
    {
        const uint vi = sh_VOffset + meshletVertices[sh_MvOffset + sh_VertexOffset + i];

        float3 posWS;
        if (isVoxel)
        {
            const VoxelVertex vertex = voxelVertices[vi];
            const float3      pos    = VoxelUnpackPosTransition(vertex, chunk.chunkSizeMeter, chunk.lodAndMask) + chunk.originWS;
            posWS = VoxelMorphPosWS(vertex, pos, VoxelUnpackNormal(vertex), cameraPosWS, chunk);
        }
        else
        {
            posWS = mul(sh_LocalToWorld, float4(meshVertices[vi].position, 1.0)).xyz;
        }

        const float4 posCS = mul(viewProj, float4(posWS, 1.0));
        sh_ClipPos[i]        = posCS;
        vertices[i].position = posCS;
    }

    // Each triangle reads clip positions written by other lanes
    GroupMemoryBarrierWithGroupSync();

    StructuredBuffer< uint > meshletTriangles = GetResource(sh_MtHeapIdx);
    const uint cullFlags = sceneBuf[0].cull.cullFlags;
    const uint triBaseIdx = sh_MtOffset + sh_TriangleOffset;
    for (uint i = ti; i < sh_TriangleCount; i += 32)
    {
        const uint tPacked3 = meshletTriangles[triBaseIdx + i];
        const uint3 tri     = uint3(tPacked3 & 0xFFu, (tPacked3 >> 8) & 0xFFu, (tPacked3 >> 16) & 0xFFu);

        triangles[i] = tri;

        primAttrs[i].cullPrimitive = isVoxel ? false : TriangleCull(sh_ClipPos[tri.x], sh_ClipPos[tri.y], sh_ClipPos[tri.z], cullFlags);
        primAttrs[i].visID0        = isVoxel ? PackVisID0Voxel(instanceID) : PackVisID0Mesh(instanceID, lod, 0u);
        primAttrs[i].visID1        = isVoxel ? PackVisID1Voxel(mi - chunkBaseMI, i, 0u) : PackVisID1(mi, i);
        primAttrs[i].fade          = isVoxel ? chunk.flags : 0u;
    }
}
