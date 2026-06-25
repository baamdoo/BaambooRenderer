#define _CULL
#include "Common.hlsli"

#define VOXEL_CULL_PHASE1 0u

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_NumChunks;
    uint g_CullingPhase;
};

ConstantBuffer< DescriptorHeapIndex > g_VoxelChunks      : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_IndirectCommands : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_DrawCount        : register(b3, ROOT_CONSTANT_SPACE);


bool IsAABBOutside(float4 frustum[6], float3 aabbMin, float3 aabbMax)
{
    for (int i = 0; i < 5; ++i)
    {
        const float4 pl = frustum[i];
        const float3 pV = float3(
            pl.x >= 0.0f ? aabbMax.x : aabbMin.x,
            pl.y >= 0.0f ? aabbMax.y : aabbMin.y,
            pl.z >= 0.0f ? aabbMax.z : aabbMin.z);

        if (dot(pl.xyz, pV) + pl.w < 0.0f)
            return true;
    }

    return false;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint chunkID = DTid.x;
    if (chunkID >= g_NumChunks)
        return;

    // Only Phase 1 emits; Phase 2's cleared draw count keeps the second pass empty.
    if (g_CullingPhase != VOXEL_CULL_PHASE1)
        return;

    StructuredBuffer< VoxelChunk > Chunks = GetResource(g_VoxelChunks.index);
    VoxelChunk chunk = Chunks[chunkID];

    // Empty chunks (all-air / all-solid) carry no meshlets — never emit a draw.
    if (chunk.meshletCount == 0u)
        return;

    const float3 aabbMin = float3(chunk.aabbMinX, chunk.aabbMinY, chunk.aabbMinZ);
    const float3 aabbMax = float3(chunk.aabbMaxX, chunk.aabbMaxY, chunk.aabbMaxZ);
    if (IsAABBOutside(g_CullData.frustum, aabbMin, aabbMax))
        return;

    RWByteAddressBuffer DrawCount = GetResource(g_DrawCount.index);
    uint outID;
    DrawCount.InterlockedAdd(0, 1, outID);

    RWStructuredBuffer< IndirectCommandData > IndirectCommands = GetResource(g_IndirectCommands.index);
    IndirectCommands[outID].drawID      = chunkID;
    IndirectCommands[outID].groupCountX = chunk.meshletCount;
    IndirectCommands[outID].groupCountY = 1u;
    IndirectCommands[outID].groupCountZ = 1u;
}
