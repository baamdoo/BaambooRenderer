#include "ShaderShared/Scene.h"
#include "ShaderShared/Mesh.h"
#include "ShaderShared/Material.h"
#include "ShaderShared/VoxelTerrain.h"
#include "Common/GBuffer.slangh"
#include "Common/DrawID.slangh"
#include "Common/Culling.slangh"
#include "Common/VoxelTerrain.slangh"

#if PROFILING_LEVEL >= 1
// Per-phase cull stats: meshlets drawn, meshlet candidates examined, triangles of the drawn meshlets
static const uint kStatsDrawn   = 0u;
static const uint kStatsTotal   = 1u;
static const uint kStatsTriCand = 2u;
#endif

groupshared AmplificationPayload Payload;

groupshared float4x4 sh_LocalToWorld;
groupshared float3   sh_CameraLocal;
groupshared float    sh_MaxScale;
groupshared uint     sh_VisOffset;
groupshared uint     sh_MeshletOffset;
groupshared uint     sh_MeshletCount;
groupshared uint     sh_VoxelChunkIndex;

[shader("amplification")]
[numthreads(32, 1, 1)]
void main(uint3 Gid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    StructuredBuffer< SceneGlobals > sceneBuf = GetResource(g_Push.ID_Scene);
    StructuredBuffer< uint >         drawIDs  = GetResource(g_Push.ID_DrawIDs);

    const uint   drawID    = drawIDs[GetDrawID()];
    const uint   phase     = g_Push.constants.phase;
    const CullData cull    = sceneBuf[0].cull;
    const float3 cameraPos = sceneBuf[0].frozenCamera.position;

    if (GTid.x == 0)
    {
        const SceneBufferIndices buffers = sceneBuf[0].buffers;

        StructuredBuffer< MeshData >      meshes     = GetResource(buffers.meshes);
        StructuredBuffer< InstanceData >  instances  = GetResource(buffers.instances);
        StructuredBuffer< TransformData > transforms = GetResource(buffers.transforms);
        StructuredBuffer< MaterialData >  materials  = GetResource(buffers.materials);

        const uint          instanceID = DrawInstanceID(drawID);
        const uint          lod        = DrawLOD(drawID);
        const InstanceData  instance   = instances[instanceID];
        const MeshData      mesh       = meshes[instance.meshID];
        const TransformData transform  = transforms[instance.transformID];

        // Medium boundaries write neither depth nor IDs
        const bool bMediumBoundary = instance.isVoxel == 0u && instance.materialID != kInvalidIndex &&
                                     (materials[instance.materialID].materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u;

        sh_LocalToWorld    = transform.mLocalToWorld;
        sh_CameraLocal     = mul(transform.mWorldToLocal, float4(cameraPos, 1.0)).xyz;
        sh_MaxScale        = MaxAxisScale(transform.mLocalToWorld);
        sh_VisOffset       = instance.visOffset;
        sh_MeshletOffset   = mesh.lods[lod].mOffset;
        sh_MeshletCount    = bMediumBoundary ? 0u : mesh.lods[lod].mCount;
        sh_VoxelChunkIndex = instance.isVoxel != 0u ? (instanceID - kVoxelChunkInstanceBase) : kInvalidIndex;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint localMeshletIdx = Gid.x;
    const uint mi              = localMeshletIdx + sh_MeshletOffset;

    const uint vi        = sh_VisOffset + localMeshletIdx;
    const uint visWord   = vi >> 5;
    const uint visBitMsk = 1u << (vi & 31u);

    const bool bValid = localMeshletIdx < sh_MeshletCount;
    bool       accept = false;

    uint slotLm       = 0u; // voxel dice budget level of this lane's meshlet
    uint slotTriCount = 0u;
#if PROFILING_LEVEL >= 1
    uint dbgTriCount = 0u;
#endif

    if (bValid && sh_VoxelChunkIndex != kInvalidIndex)
    {
        StructuredBuffer< VoxelChunkDesc > chunks   = GetResource(g_Push.ID_VoxelChunkDescs);
        StructuredBuffer< Meshlet >        meshlets = GetResource(g_Push.ID_VoxelMeshlets);

        const VoxelChunkDesc chunk   = chunks[sh_VoxelChunkIndex];
        const Meshlet        meshlet = meshlets[mi];

        const float3 centerWS = meshlet.center + chunk.originWS;
        const float  radiusWS = meshlet.radius * sh_MaxScale;

        accept = true;
        if ((cull.cullFlags & kCullFlagMeshletFrustum) != 0u)
            accept = !IsFrustumCulled(cull.frustum, centerWS, radiusWS);

        // Voxel chunks have no meshlet occlusion; phase 2 draws only chunks phase 1 skipped
        if (phase == kCullPhase2 && DrawEarly(drawID))
            accept = false;

        if (accept && chunk.diceMaxLevel != 0u)
        {
            slotLm       = DiceMeshletBudgetLevel(centerWS, radiusWS, cameraPos, chunk);
            slotTriCount = meshlet.triangleCount;
        }
    }
    else if (bValid)
    {
        RWStructuredBuffer< uint > meshletVisibility = GetResource(g_Push.ID_MeshletVisibility);

        const bool bPrevVis   = (meshletVisibility[visWord] & visBitMsk) != 0u;
        const bool bOcclusion = (cull.cullFlags & kCullFlagMeshletOcclusion) != 0u;

        const bool bSkipPhase1 = phase == kCullPhase1 && bOcclusion && !bPrevVis;
        if (!bSkipPhase1)
        {
            StructuredBuffer< Meshlet > meshlets = GetResource(sceneBuf[0].buffers.meshlets);
            const Meshlet meshlet = meshlets[mi];

            const float3 centerWS = mul(sh_LocalToWorld, float4(meshlet.center, 1.0)).xyz;
            const float  radiusWS = meshlet.radius * sh_MaxScale;

            accept = true;
            if ((cull.cullFlags & kCullFlagMeshletFrustum) != 0u && accept)
                accept = !IsFrustumCulled(cull.frustum, centerWS, radiusWS);

            // The sign of dot(n, p - e) survives the affine map, so the local-space test is exact
            if ((cull.cullFlags & kCullFlagMeshletCone) != 0u && accept)
                accept = !IsConeCulled(float4(meshlet.coneAxis, meshlet.coneCutoff), meshlet.center, meshlet.radius, sh_CameraLocal);

            if (bOcclusion && accept && phase == kCullPhase2)
            {
                Texture2D< float > hiZ = GetResource(g_Push.ID_HiZTexture);
                accept = !IsOccluded(centerWS, radiusWS, sceneBuf[0].camera, hiZ, uint2(cull.hiZWidth, cull.hiZHeight));
            }

            // Lanes share a word, so read bPrevVis before the update; the bits are stale unless phase 1 drew the instance
            if (phase == kCullPhase2 && bOcclusion)
            {
                if (accept) InterlockedOr(meshletVisibility[visWord], visBitMsk);
                else        InterlockedAnd(meshletVisibility[visWord], ~visBitMsk);

                if (bPrevVis && DrawEarly(drawID))
                    accept = false;
            }

#if PROFILING_LEVEL >= 1
            if (accept)
                dbgTriCount = meshlet.triangleCount;
#endif
        }
    }

    const uint payloadIndex = WavePrefixCountBits(accept);

    // Per-slot mesh group budget
    const uint groups    = DiceGroupsForMeshlet(max(slotLm, 1u), slotTriCount);
    const uint numGroups = accept ? ((slotLm > 0u) ? groups : 1u) : 0u;
    const uint diceMask  = WaveActiveBitOr((accept && slotLm > 0u) ? (1u << payloadIndex) : 0u);

    [unroll]
    for (uint w = 0; w < 4; ++w)
    {
        const uint lmBits = WaveActiveBitOr((accept && (payloadIndex >> 3u) == w) ? (slotLm << ((payloadIndex & 7u) * 4u)) : 0u);
        if (GTid.x == 0)
            Payload.LmPacked[w] = lmBits;
    }

    [unroll]
    for (uint w = 0; w < 8; ++w)
    {
        const uint bits = WaveActiveBitOr((accept && (payloadIndex >> 2u) == w) ? (slotTriCount << ((payloadIndex % 4u) * 8u)) : 0u);
        if (GTid.x == 0)
            Payload.triCountPacked[w] = bits;
    }

    const uint laneMask    = WaveActiveBallot(accept).x;
    const uint totalGroups = WaveActiveSum(numGroups);

    if (GTid.x == 0)
    {
        Payload.drawID        = drawID;
        Payload.diceMask      = diceMask;
        Payload.tsLaneBits    = laneMask;
        Payload.miBaseGroupID = mi;
    }

#if PROFILING_LEVEL >= 1
    const uint numMeshlets = WaveActiveCountBits(accept);
    const uint numValid    = WaveActiveCountBits(bValid);
    const uint numTris     = WaveActiveSum(dbgTriCount);
    if (WaveIsFirstLane())
    {
        RWStructuredBuffer< uint > meshletStats = GetResource(g_Push.ID_MeshletStats);
        InterlockedAdd(meshletStats[kStatsDrawn], numMeshlets);
        InterlockedAdd(meshletStats[kStatsTotal], numValid);
        InterlockedAdd(meshletStats[kStatsTriCand], numTris);
    }
#endif

    DispatchMesh(totalGroups, 1, 1, Payload);
}
