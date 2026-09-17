#define _MATERIAL
#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_VoxelChunkDescs : register(b8, ROOT_CONSTANT_SPACE);

static uint s_BayerMatrix[4][4] = { { 0, 8, 2, 10 } ,{ 12, 4, 14, 6 },{ 3, 11, 1, 9 },{ 15, 7, 13, 5 } };

struct PSInput
{
    float4 position : SV_Position;

    nointerpolation uint visID0 : ID1;
    nointerpolation uint visID1 : ID2;
};

struct PSOutput
{
    uint VBuf0 : SV_Target0;  // visibility surface ID
    uint VBuf1 : SV_Target1;  // visibility primitive ID
};

PSOutput main(PSInput input)
{
    PSOutput output = (PSOutput)0;

    if (VisIsVoxel(input.visID0))
    {
        StructuredBuffer< VoxelChunkDesc > VoxelChunkDescs = GetResource(g_VoxelChunkDescs.index);

        VoxelChunkDesc chunk = VoxelChunkDescs[VisChunkIndex(input.visID0) - VOXEL_CHUNK_INSTANCE_BASE];
        uint fadeThreshold = (chunk.flags >> 8u) & 0x1Fu;
        bool bFadeOut = (chunk.flags & 2u) != 0u;

		uint bayer = s_BayerMatrix[uint(input.position.x) & 3u][uint(input.position.y) & 3u];
        if (bFadeOut ? bayer < fadeThreshold : bayer >= fadeThreshold)
            clip(-1);
    }

    output.VBuf0 = input.visID0;
    output.VBuf1 = input.visID1;
    return output;
}
