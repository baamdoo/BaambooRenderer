#define _CAMERA
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

    uint g_CullFlags;
    uint g_Phase;    
    uint g_HiZWidth; 
    uint g_HiZHeight;
};

ConstantBuffer< DescriptorHeapIndex > g_HiZTexture              : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MeshletVisibilityBuffer : register(b2, ROOT_CONSTANT_SPACE);

#if PROFILING_LEVEL >= 1
ConstantBuffer< DescriptorHeapIndex > g_MeshletStats            : register(b3, ROOT_CONSTANT_SPACE);

// Stage 6: per-phase cull stats counter. Must match GBufferNode::MESHLET_STATS_FIELDS layout:
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

[numthreads(32, 1, 1)]
void main(uint3 Gid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    InstanceData  instance  = Instances[g_DrawID];
    MeshData      mesh      = Meshes[instance.meshID];
    TransformData transform = Transforms[instance.transformID];

    float scaleX = length(transform.mLocalToWorld[0].xyz);
    float scaleY = length(transform.mLocalToWorld[1].xyz);
    float scaleZ = length(transform.mLocalToWorld[2].xyz);
    float maxScale = max(scaleX, max(scaleY, scaleZ));

    float4 center = mul(transform.mLocalToWorld, float4(mesh.centerX, mesh.centerY, mesh.centerZ, 1.0));
    float  radius = mesh.radius * maxScale;

    uint lod = CalculateLODLevelSSE(
        mesh, center.xyz, maxScale,
        g_Camera.posWORLD, g_Camera.mProj[1][1],
        g_CullData.viewportHeight, g_CullData.sseThresholdPx);

    uint localMeshletIdx = Gid.x;
    uint mi              = localMeshletIdx + mesh.lods[lod].mOffset;

    uint vi        = instance.visOffset + localMeshletIdx;
    uint visWord   = vi >> 5;
    uint visBitMsk = 1u << (vi & 31u);

    bool bValid = (localMeshletIdx < mesh.lods[lod].mCount);
    bool accept = false;
#if PROFILING_LEVEL >= 1
    uint myTriCount = 0u; // triangleCount of THIS thread's meshlet, only when accept
#endif

    if (bValid)
    {
        RWStructuredBuffer< uint > MeshletVisibility = GetResource(g_MeshletVisibilityBuffer.index);

        bool bPrevVis   = (MeshletVisibility[visWord] & visBitMsk) != 0u;
        bool bOcclusion = (g_CullFlags & CULL_FLAG_MESHLET_OCCLUSION) != 0u;

        // Phase 1 skip: only emit meshlets that were visible last frame.
        bool bSkipPhase1 = (g_Phase == PHASE1_CULL) && bOcclusion && !bPrevVis;
        if (!bSkipPhase1)
        {
            Meshlet meshlet = Meshlets[mi];

            // World-space meshlet bound (scaled by instance transform)
            float3 meshletCenterWS = mul(transform.mLocalToWorld, float4(meshlet.centerX, meshlet.centerY, meshlet.centerZ, 1.0)).xyz;
            float  meshletRadiusWS = meshlet.radius * maxScale;

            accept = true;

            // 1) Frustum cull
            if ((g_CullFlags & CULL_FLAG_MESHLET_FRUSTUM) != 0u && accept)
            {
                accept = !IsFrustumCulled(g_CullData.frustum, meshletCenterWS, meshletRadiusWS);
            }

            // 2) Backface cone cull
            if ((g_CullFlags & CULL_FLAG_MESHLET_CONE) != 0u && accept)
            {
                float3 coneAxisWS = normalize(mul(transform.mLocalToWorld, float4(meshlet.coneAxisX, meshlet.coneAxisY, meshlet.coneAxisZ, 0.0)).xyz);

                accept = !IsConeCulled(float4(coneAxisWS, meshlet.coneCutoff), meshletCenterWS, meshletRadiusWS, g_Camera.posWORLD);
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
                    g_HiZWidth,
                    g_HiZHeight);
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
        Payload.lodLevel = lod;

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
