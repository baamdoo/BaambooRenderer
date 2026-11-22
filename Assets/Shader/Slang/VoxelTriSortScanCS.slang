#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"

struct VoxelTriSortScanPush
{
    uint ID_SortBins;
};

[[vk::push_constant]] ConstantBuffer< VoxelTriSortScanPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

groupshared uint sh_StripSum[kVoxelTriSortScanThreads];

// Exclusive scan of the sort bins in one group: each thread owns a contiguous strip of bins
[numthreads(kVoxelTriSortScanThreads, 1, 1)]
void main(uint3 gt : SV_GroupThreadID)
{
    uint ti            = gt.x;
    uint binsPerThread = kVoxelTriSortBins / kVoxelTriSortScanThreads;
    uint base          = ti * binsPerThread;

    RWStructuredBuffer< uint > bins = GetResource(g_Push.ID_SortBins);

    uint sum = 0u;
    for (uint i = 0u; i < binsPerThread; ++i)
        sum += bins[base + i];
    sh_StripSum[ti] = sum;
    GroupMemoryBarrierWithGroupSync();

    // Hillis-Steele inclusive scan (read neighbour, barrier, then write own slot)
    for (uint offset = 1u; offset < kVoxelTriSortScanThreads; offset <<= 1u)
    {
        uint v = (ti >= offset) ? sh_StripSum[ti - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        sh_StripSum[ti] += v;
        GroupMemoryBarrierWithGroupSync();
    }

    // inclusive - own = exclusive strip start; rewrite own strip in place
    uint run = sh_StripSum[ti] - sum;
    for (uint i = 0u; i < binsPerThread; ++i)
    {
        uint c = bins[base + i];
        bins[base + i] = run;
        run += c;
    }
}
