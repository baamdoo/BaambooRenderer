#define _CAMERA
#define _FROZENCAMERA
#define _TRANSFORM
#define _MESH
#define _CULL
#include "Common.hlsli"
#include "HelperFunctions.hlsli"
#include "CullingCommon.hlsli"


#define CULL_FLAG_MESHLET_FRUSTUM   4u
#define CULL_FLAG_MESHLET_CONE      8u
#define CULL_FLAG_MESHLET_OCCLUSION 16u

#define PHASE1_CULL 0u
#define PHASE2_CULL 1u


cbuffer CommandSignatureParam : register(b0, COMMMANDSIGNATURE_SPACE)
{
    uint g_DrawID;
};

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float2 g_Viewport;
    uint   g_Phase;
};

ConstantBuffer< DescriptorHeapIndex > g_HiZTexture              : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MeshletVisibilityBuffer : register(b2, ROOT_CONSTANT_SPACE);

#if PROFILING_LEVEL >= 1
ConstantBuffer< DescriptorHeapIndex > g_MeshletStats            : register(b3, ROOT_CONSTANT_SPACE);

// Per-phase cull stats counter. Must match GBufferNode::MESHLET_STATS_FIELDS layout:
//   [0] = meshletDrawn         — meshlets that survived task-shader cull
//   [1] = meshletTotal         — meshlet candidates examined (incl. bSkipPhase1 ones)
//   [2] = triangleCandidates   — sum of triangleCount for drawn meshlets (= mesh shader input)
#define STATS_DRAWN     0u
#define STATS_TOTAL     1u
#define STATS_TRI_CAND  2u
#endif // PROFILING_LEVEL >= 1


static StructuredBuffer< Meshlet >       Meshlets   = GetResource(g_Meshlets.index);
static StructuredBuffer< MeshData >      Meshes     = GetResource(g_Meshes.index);
static StructuredBuffer< InstanceData >  Instances  = GetResource(g_Instances.index);
static StructuredBuffer< TransformData > Transforms = GetResource(g_Transforms.index);


struct AmplificationPayload
{
    uint lodLevel;
    uint meshletIndices[32];
};

groupshared AmplificationPayload Payload;

// Lane 0 loads everything once, derived offsets are cached in shared memory to reduce LSGB stall.
groupshared float4x4 sh_LocalToWorld;
groupshared float    sh_MaxScale;
groupshared uint     sh_VisOffset;
groupshared uint     sh_Lod;
groupshared uint     sh_MeshletOffset;
groupshared uint     sh_MeshletCount;

[numthreads(32, 1, 1)]
void main(uint3 Gid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    if (GTid.x == 0)
    {
        // Unpack (lod << 24) | instanceID — Instance Culling already chose the LOD; we just consume it.
        uint instanceID = g_DrawID & 0x00FFFFFFu;
        uint lod        = g_DrawID >> 24;

        InstanceData  instance  = Instances[instanceID];
        MeshData      mesh      = Meshes[instance.meshID];
        TransformData transform = Transforms[instance.transformID];

        float scaleX = length(transform.mLocalToWorld[0].xyz);
        float scaleY = length(transform.mLocalToWorld[1].xyz);
        float scaleZ = length(transform.mLocalToWorld[2].xyz);
        float maxScale = max(scaleX, max(scaleY, scaleZ));

        sh_LocalToWorld  = transform.mLocalToWorld;
        sh_MaxScale      = maxScale;
        sh_VisOffset     = instance.visOffset;
        sh_Lod           = lod;
        sh_MeshletOffset = mesh.lods[lod].mOffset;
        sh_MeshletCount  = mesh.lods[lod].mCount;
    }
    GroupMemoryBarrierWithGroupSync();

    uint localMeshletIdx = Gid.x;
    uint mi              = localMeshletIdx + sh_MeshletOffset;

    uint vi        = sh_VisOffset + localMeshletIdx;
    uint visWord   = vi >> 5;
    uint visBitMsk = 1u << (vi & 31u);

    bool bValid = (localMeshletIdx < sh_MeshletCount);
    bool accept = false;
#if PROFILING_LEVEL >= 1
    uint myTriCount = 0u; // triangleCount of THIS thread's meshlet, only when accept
#endif

    if (bValid)
    {
        RWStructuredBuffer< uint > MeshletVisibility = GetResource(g_MeshletVisibilityBuffer.index);

        bool bPrevVis   = (MeshletVisibility[visWord] & visBitMsk) != 0u;
        bool bOcclusion = (g_CullData.cullFlags & CULL_FLAG_MESHLET_OCCLUSION) != 0u;

        // Phase 1 skip: only emit meshlets that were visible last frame.
        bool bSkipPhase1 = (g_Phase == PHASE1_CULL) && bOcclusion && !bPrevVis;
        if (!bSkipPhase1)
        {
            Meshlet meshlet = Meshlets[mi];

            // World-space meshlet bound (scaled by instance transform)
            float3 meshletCenterWS = mul(sh_LocalToWorld, float4(meshlet.centerX, meshlet.centerY, meshlet.centerZ, 1.0)).xyz;
            float  meshletRadiusWS = meshlet.radius * sh_MaxScale;

            accept = true;

            // 1) Frustum cull
            if ((g_CullData.cullFlags & CULL_FLAG_MESHLET_FRUSTUM) != 0u && accept)
            {
                accept = !IsFrustumCulled(g_CullData.frustum, meshletCenterWS, meshletRadiusWS);
            }

            // 2) Backface cone cull
            if ((g_CullData.cullFlags & CULL_FLAG_MESHLET_CONE) != 0u && accept)
            {
                float3 coneAxisWS = normalize(mul(sh_LocalToWorld, float4(meshlet.coneAxisX, meshlet.coneAxisY, meshlet.coneAxisZ, 0.0)).xyz);

                accept = !IsConeCulled(float4(coneAxisWS, meshlet.coneCutoff), meshletCenterWS, meshletRadiusWS, g_FrozenCamera.posWORLD);
            }

            // 3) HiZ occlusion cull (Phase 2 only; pyramid rebuilt from Phase 1 depth)
            if (bOcclusion && accept && g_Phase == PHASE2_CULL)
            {
                Texture2D< float > HiZ = GetResource(g_HiZTexture.index);
                bool bOccluded = IsOccluded(
                    meshletCenterWS,
                    meshletRadiusWS,
                    g_Camera.mView,
                    g_Camera.mProj[0][0],
                    g_Camera.mProj[1][1],
                    g_Camera.zNear,
                    HiZ,
                    g_LinearClampMinSampler,
                    g_CullData.hiZWidth,
                    g_CullData.hiZHeight);
                accept = !bOccluded;
            }

            // 4) Phase 2 visibility update + suppress emission for already-drawn meshlets.
            // Adjacent threads may write to the same u32 word -> atomics required.
            if (g_Phase == PHASE2_CULL && bOcclusion)
            {
                if (accept) InterlockedOr(MeshletVisibility[visWord],  visBitMsk);
                else        InterlockedAnd(MeshletVisibility[visWord], ~visBitMsk);

                if (bPrevVis) accept = false; // already emitted by Phase 1
            }

#if PROFILING_LEVEL >= 1
            if (accept)
                myTriCount = meshlet.triangleCount;
#endif
        }
    }

    uint payloadIndex = WavePrefixCountBits(accept);
    if (accept)
        Payload.meshletIndices[payloadIndex] = mi;

    if (GTid.x == 0)
        Payload.lodLevel = sh_Lod;

    uint numMeshlets = WaveActiveCountBits(accept);

#if PROFILING_LEVEL >= 1
    uint numValid = WaveActiveCountBits(bValid);
    uint numTris  = WaveActiveSum(myTriCount);
    if (WaveIsFirstLane())
    {
        RWStructuredBuffer< uint > MeshletStats = GetResource(g_MeshletStats.index);
        InterlockedAdd(MeshletStats[STATS_DRAWN],    numMeshlets);
        InterlockedAdd(MeshletStats[STATS_TOTAL],    numValid);
        InterlockedAdd(MeshletStats[STATS_TRI_CAND], numTris);
    }
#endif // PROFILING_LEVEL >= 1
    DispatchMesh(numMeshlets, 1, 1, Payload);
}
