#include "Common.hlsli"

#define SCAN_THREADS 1024u

cbuffer TriSortScanPushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_NumBins; // multiple of SCAN_THREADS; 32768 for 32^3 blocks
};

ConstantBuffer< DescriptorHeapIndex > g_SortBins : register(b1, ROOT_CONSTANT_SPACE);

groupshared uint s_StripSum[SCAN_THREADS];

[numthreads(SCAN_THREADS, 1, 1)]
void main(uint3 gt : SV_GroupThreadID)
{
    uint ti            = gt.x;
    uint binsPerThread = g_NumBins / SCAN_THREADS;
    uint base          = ti * binsPerThread;

    RWStructuredBuffer< uint > Bins = GetResource(g_SortBins.index);

    uint sum = 0u;
    for (uint i = 0u; i < binsPerThread; ++i)
        sum += Bins[base + i];
    s_StripSum[ti] = sum;
    GroupMemoryBarrierWithGroupSync();

    // Hillis-Steele inclusive scan (read neighbour, barrier, then write own slot)
    for (uint offset = 1u; offset < SCAN_THREADS; offset <<= 1u)
    {
        uint v = (ti >= offset) ? s_StripSum[ti - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        s_StripSum[ti] += v;
        GroupMemoryBarrierWithGroupSync();
    }

    // inclusive - own = exclusive strip start; rewrite own strip in place
    uint run = s_StripSum[ti] - sum;
    for (uint i = 0u; i < binsPerThread; ++i)
    {
        uint c = Bins[base + i];
        Bins[base + i] = run;
        run += c;
    }
}
