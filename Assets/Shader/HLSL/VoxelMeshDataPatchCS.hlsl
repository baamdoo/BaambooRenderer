#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_VoxelMeshBaseID;
    uint g_NumVoxelSlots;
};

ConstantBuffer< DescriptorHeapIndex > g_VoxelCounts     : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelChunkDescs : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MeshData        : register(b3, ROOT_CONSTANT_SPACE);

[numthreads(64, 1, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    StructuredBuffer< VoxelChunkCounts > Counts     = GetResource(g_VoxelCounts.index);
    StructuredBuffer< VoxelChunkDesc >   Chunks     = GetResource(g_VoxelChunkDescs.index);
    RWStructuredBuffer< MeshData >       MeshBuffer = GetResource(g_MeshData.index);

	uint ti = tID.x;
    if (ti >= g_NumVoxelSlots)
        return;

	VoxelChunkDesc chunk = Chunks[ti];

	float3 originWS          = float3(chunk.originX, chunk.originY, chunk.originZ);
	float  halfLengthPerAxis = 0.5 * chunk.chunkSizeMeter; 

	MeshData meshData = MeshBuffer[g_VoxelMeshBaseID + ti];
    meshData.vOffset = chunk.vOffset;
    meshData.radius  = halfLengthPerAxis * 1.7320508; // half-diagonal of a cube = 0.5 * sqrt(3) * size
	meshData.centerX = originWS.x + halfLengthPerAxis; meshData.centerY = originWS.y + halfLengthPerAxis; meshData.centerZ = originWS.z + halfLengthPerAxis;

    meshData.lods[0].mCount   = (chunk.pageID != INVALID_INDEX && (chunk.flags & 1u) != 0u) ? Counts[ti].meshletCount : 0u;
    meshData.lods[0].mOffset  = chunk.mOffset;
    meshData.lods[0].mvOffset = chunk.mvOffset;
    meshData.lods[0].mtOffset = chunk.mtOffset;

    MeshBuffer[g_VoxelMeshBaseID + ti] = meshData;
}
