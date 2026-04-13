#ifndef _SPD_COMMON_HLSLI
#define _SPD_COMMON_HLSLI

// Reusable SPD (Single Pass Downsampler) integration layer for HLSL.
//
// Implements the 5 callbacks that are identical across all SPD effects:
//   SpdLoadIntermediate, SpdStoreIntermediate,
//   SpdIncreaseAtomicCounter, SpdGetAtomicCounter, SpdResetAtomicCounter
//
// Before including this header, the per-effect shader MUST:
//   1. #include "Common.hlsli"  (for GetResource / DescriptorHeapIndex)
//   2. Declare: ConstantBuffer<DescriptorHeapIndex> g_SPDCounter : register(bN, ROOT_CONSTANT_SPACE);
//   3. Optionally #define SPD_SINGLE_CHANNEL for single-channel effects (e.g., Hi-Z)
//
// After including this header, the per-effect shader MUST:
//   1. Define 4 per-effect callbacks: SpdLoadSourceImage, SpdLoad, SpdStore, SpdReduce4
//   2. #include "../FidelityFX/ffx_spd.h"  (the SPD algorithm, NOT included here)
//   3. Define the compute entry point calling SpdDownsample()

// FidelityFX type aliases (AF4, AU1, ASU2, etc.)
#define A_GPU
#define A_HLSL
#include "../FidelityFX/ffx_a.h"

// ============================================================================
// LDS (Local Data Store) declarations
// ============================================================================
#ifdef SPD_SINGLE_CHANNEL
groupshared AF1 spdIntermediateR[16][16];
#else
groupshared AF1 spdIntermediateR[16][16];
groupshared AF1 spdIntermediateG[16][16];
groupshared AF1 spdIntermediateB[16][16];
groupshared AF1 spdIntermediateA[16][16];
#endif

groupshared AU1 spdCounter;

// ============================================================================
// Common callback: LDS load
// ============================================================================
AF4 SpdLoadIntermediate(AU1 x, AU1 y)
{
#ifdef SPD_SINGLE_CHANNEL
    return AF4(spdIntermediateR[x][y], 0, 0, 0);
#else
    return AF4(spdIntermediateR[x][y], spdIntermediateG[x][y],
               spdIntermediateB[x][y], spdIntermediateA[x][y]);
#endif
}

// ============================================================================
// Common callback: LDS store
// ============================================================================
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value)
{
    spdIntermediateR[x][y] = value.x;
#ifndef SPD_SINGLE_CHANNEL
    spdIntermediateG[x][y] = value.y;
    spdIntermediateB[x][y] = value.z;
    spdIntermediateA[x][y] = value.w;
#endif
}

// ============================================================================
// Common callback: atomic counter increment
//   Reads counter into groupshared spdCounter for SpdGetAtomicCounter().
//   Requires g_SPDCounter (ConstantBuffer<DescriptorHeapIndex>) from per-effect shader.
// ============================================================================
void SpdIncreaseAtomicCounter(AU1 slice)
{
    RWByteAddressBuffer counter = GetResource(g_SPDCounter.index);
    counter.InterlockedAdd(0, 1, spdCounter);
}

// ============================================================================
// Common callback: read atomic counter (groupshared, set by SpdIncreaseAtomicCounter)
// ============================================================================
AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}

// ============================================================================
// Common callback: reset atomic counter to 0
// ============================================================================
void SpdResetAtomicCounter(AU1 slice)
{
    RWByteAddressBuffer counter = GetResource(g_SPDCounter.index);
    counter.Store(0, 0);
}

#endif // _SPD_COMMON_HLSLI
