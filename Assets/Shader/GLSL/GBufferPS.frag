#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_mesh_shader : require

#define _MATERIAL
#include "DescriptorCommon.hg"
#include "VisibilityBuffer.hg"

layout(location = 6) perprimitiveEXT flat in uint inVisID0;
layout(location = 7) perprimitiveEXT flat in uint inVisID1;

layout(location = 0) out uint outVBuf0;    // visibility surface ID
layout(location = 1) out uint outVBuf1;    // visibility primitive ID

layout(set = 1, binding = 8) readonly buffer VoxelChunkDescBuffer { VoxelChunkDesc g_VoxelChunkDescs[]; };

const uint s_BayerMatrix[16] = uint[16](0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u);

void main()
{
    if (VisIsVoxel(inVisID0))
    {
        VoxelChunkDesc chunk = g_VoxelChunkDescs[VisChunkIndex(inVisID0) - VOXEL_CHUNK_INSTANCE_BASE];
        uint fadeT    = (chunk.flags >> 8u) & 0x1Fu;
        bool bFadeOut = (chunk.flags & 2u) != 0u;

        uint bayer = s_BayerMatrix[(uint(gl_FragCoord.x) & 3u) * 4u + (uint(gl_FragCoord.y) & 3u)];
        if (bFadeOut ? bayer < fadeT : bayer >= fadeT)
            discard;
    }

    outVBuf0 = inVisID0;
    outVBuf1 = inVisID1;
}
