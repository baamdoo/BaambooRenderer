#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_VoxelMeshID;
};

ConstantBuffer< DescriptorHeapIndex > g_VoxelCounts : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MeshData    : register(b2, ROOT_CONSTANT_SPACE);

[numthreads(1, 1, 1)]
void main()
{
    StructuredBuffer< VoxelChunkCounts > Counts   = GetResource(g_VoxelCounts.index);
    RWStructuredBuffer< MeshData >       MeshData = GetResource(g_MeshData.index);

    MeshData[g_VoxelMeshID].lods[0].mCount = Counts[VOXEL_CHUNK_INSTANCE_BASE].meshletCount;
}
