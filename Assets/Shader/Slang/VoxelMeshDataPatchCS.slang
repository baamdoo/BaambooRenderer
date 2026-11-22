#include "ShaderShared/VoxelTerrain.h"
#include "ShaderShared/Mesh.h"
#include "Common/Binding.slangh"

struct VoxelMeshDataPatchPush
{
    VoxelMeshDataPatchConstants constants;

    uint ID_VoxelCounts;
    uint ID_VoxelChunkDescs;
    uint ID_MeshData;
};

[[vk::push_constant]] ConstantBuffer< VoxelMeshDataPatchPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

[numthreads(64, 1, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    const uint ti = tID.x;
    if (ti >= g_Push.constants.numVoxelSlots)
        return;

    StructuredBuffer< VoxelChunkCounts > counts     = GetResource(g_Push.ID_VoxelCounts);
    StructuredBuffer< VoxelChunkDesc >   chunks     = GetResource(g_Push.ID_VoxelChunkDescs);
    RWStructuredBuffer< MeshData >       meshBuffer = GetResource(g_Push.ID_MeshData);

    const VoxelChunkDesc chunk      = chunks[ti];
    const float          halfLength = 0.5 * chunk.chunkSizeMeter;
    const uint           row        = g_Push.constants.voxelMeshBaseID + ti;

    MeshData meshData = meshBuffer[row];
    meshData.vOffset = chunk.vOffset;
    meshData.center  = chunk.originWS + halfLength;
    meshData.radius  = halfLength * 1.7320508; // half-diagonal of the cube

    meshData.lods[0].mCount   = (chunk.pageID != kInvalidIndex && (chunk.flags & 1u) != 0u) ? counts[ti].meshletCount : 0u;
    meshData.lods[0].mOffset  = chunk.mOffset;
    meshData.lods[0].mvOffset = chunk.mvOffset;
    meshData.lods[0].mtOffset = chunk.mtOffset;

    meshBuffer[row] = meshData;
}
