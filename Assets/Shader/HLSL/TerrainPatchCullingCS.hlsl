#define _CAMERA
#define _FROZENCAMERA
#define _CULL
#include "Common.hlsli"
#include "TerrainCommon.hlsli"
#include "CullingCommon.hlsli"

#define TERRAIN_CULL_PHASE1  0u
#define TERRAIN_CULL_PHASE2  1u

struct TerrainNodeGPU
{
    float3 aabbMin;
    float  sizeMeter;

    float3 aabbMax;
    uint   depth;

    float2 originXZ;
    uint   gridN;
    uint   parentIdx;

    uint4  childIdx;
};

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_NumNodes;
    uint  g_CullingPhase;
    float g_LodRangeBase;
    float g_LodMorphK;
    uint  g_MaxDepth;
};

ConstantBuffer< DescriptorHeapIndex > g_TerrainNodes     : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CulledPatches    : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_IndirectCommands : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_DrawCount        : register(b4, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_NodeVisibility   : register(b5, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_HiZTexture       : register(b6, ROOT_CONSTANT_SPACE);
#if PROFILING_LEVEL >= 1
ConstantBuffer< DescriptorHeapIndex > g_LodStats         : register(b7, ROOT_CONSTANT_SPACE);
#endif


float DistanceToAABB(float3 cameraPos, float3 aabbMin, float3 aabbMax)
{
    const float3 clamped = clamp(cameraPos, aabbMin, aabbMax);
    return length(cameraPos - clamped);
}

bool IsAABBOutside(float4 frustum[6], float3 aabbMin, float3 aabbMax)
{
    const float3 mn = aabbMin;
    const float3 mx = aabbMax;
    for (int i = 0; i < 6; ++i)
    {
        const float4 pl = frustum[i];
        const float3 pV = float3(
            pl.x >= 0.0f ? mx.x : mn.x,
            pl.y >= 0.0f ? mx.y : mn.y,
            pl.z >= 0.0f ? mx.z : mn.z);

        if (dot(pl.xyz, pV) + pl.w < 0.0f)
            return true;
    }

    return false;
}

bool DoesNodeDelegateToChildren(StructuredBuffer< TerrainNodeGPU > TerrainNodes, TerrainNodeGPU node)
{
    if (node.depth == g_MaxDepth)
        return false;

    const float childRange = g_LodRangeBase * exp2(float(int(g_MaxDepth) - int(node.depth + 1u)));
    [unroll]
    for (uint c = 0u; c < 4u; ++c)
    {
        TerrainNodeGPU child = TerrainNodes[node.childIdx[c]];
        const float distChild = DistanceToAABB(g_FrozenCamera.posWORLD, child.aabbMin, child.aabbMax);
        if (distChild > childRange)
            return false;
    }

    return true;
}

[numthreads(64, 1, 1)]
void main(uint3 Gid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    uint nodeID = Gid.x;
    if (nodeID >= g_NumNodes)
        return;

    RWStructuredBuffer< uint > NodeVisibility = GetResource(g_NodeVisibility.index);
    bool bPrevVis = (NodeVisibility[nodeID] != 0u);
    if (g_CullingPhase == TERRAIN_CULL_PHASE1 && !bPrevVis)
        return;

    StructuredBuffer< TerrainNodeGPU > TerrainNodes = GetResource(g_TerrainNodes.index);
    TerrainNodeGPU node = TerrainNodes[nodeID];

    const float re         = g_LodRangeBase * exp2(float(int(g_MaxDepth) - int(node.depth)));
    const float distToNode = DistanceToAABB(g_FrozenCamera.posWORLD, node.aabbMin, node.aabbMax);
    if (distToNode > re)
        return;

    if (node.parentIdx != 0xFFFFFFFFu)
    {
        TerrainNodeGPU parent = TerrainNodes[node.parentIdx];
        if (!DoesNodeDelegateToChildren(TerrainNodes, parent))
            return;
    }

    if (DoesNodeDelegateToChildren(TerrainNodes, node))
        return;

    if (IsAABBOutside(g_CullData.frustum, node.aabbMin, node.aabbMax))
        return;

    if (g_CullingPhase == TERRAIN_CULL_PHASE2)
    {
        if (IsAABBOccluded(node.aabbMin, node.aabbMax,
                           g_Camera.mView, g_Camera.mProj, g_Camera.zNear,
                           GetResource(g_HiZTexture.index),
                           g_LinearClampMinSampler,
                           g_CullData.hiZWidth, g_CullData.hiZHeight))
        {
            NodeVisibility[nodeID] = 0u;
            return;
        }

        NodeVisibility[nodeID] = 1u;
    }

    if (g_CullingPhase == TERRAIN_CULL_PHASE1 || bPrevVis == 0)
    {
        RWByteAddressBuffer DrawCount = GetResource(g_DrawCount.index);
        uint outID;
        DrawCount.InterlockedAdd(0, 1, outID);

#if PROFILING_LEVEL >= 1
        RWByteAddressBuffer LodStats = GetResource(g_LodStats.index);
        LodStats.InterlockedAdd(node.depth * 4u, 1u);
#endif

        RWStructuredBuffer< PatchInstance > CulledPatches = GetResource(g_CulledPatches.index);
        PatchInstance instance = { node.originXZ.x, node.originXZ.y, node.sizeMeter, node.depth, node.gridN };
        CulledPatches[outID] = instance;

        RWStructuredBuffer< IndirectCommandData > IndirectCommands = GetResource(g_IndirectCommands.index);
        IndirectCommands[outID].drawID = outID;
        IndirectCommands[outID].groupCountX = 1;
        IndirectCommands[outID].groupCountY = 1;
        IndirectCommands[outID].groupCountZ = 1;
    }
}
