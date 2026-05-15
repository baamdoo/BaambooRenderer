#include "Common.hlsli"
#include "LightCullingCommon.hlsli"


cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_NumClusters;
};

ConstantBuffer< DescriptorHeapIndex > g_LightGridBuffer : register(b1, ROOT_CONSTANT_SPACE);


[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x != 0) return;

    RWStructuredBuffer< uint2 > LightGrid = GetResource(g_LightGridBuffer.index);

    uint cumulativeOffset = 0;
    [loop] for (uint i = 0; i < g_NumClusters; ++i)
    {
        uint2 entry = LightGrid[i];
        LightGrid[i] = uint2(cumulativeOffset, entry.y);
        cumulativeOffset += entry.y;
    }
}
