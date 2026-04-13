// Hi-Z pyramid generation via AMD FidelityFX SPD (Single Pass Downsampler).
//
// Generates the full Hi-Z mip chain in a single compute dispatch.
// Uses min reduction for reversed-Z depth (0=far, 1=near).
//
// The Hi-Z pyramid is sized to previousPow2(window) so every 2x2 reduction
// is exact (niagara-style). Because the pyramid is smaller than the depth
// buffer, Hi-Z mip 0 is computed by sampling the depth buffer through a
// MIN reduction sampler at scaled UV coordinates; SPD then reads from mip 0
// and produces mips 1+ with exact 2x2 reductions.

#define _CAMERA
#include "Common.hlsli"

// ============================================================================
// Push constants
// ============================================================================
cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_Mip0Width;
    uint g_Mip0Height;
    uint g_SpdMipCount;
    uint g_NumWorkGroups;
};

// ============================================================================
// Bindless descriptors
// ============================================================================
ConstantBuffer< DescriptorHeapIndex > g_DepthSrc   : register(b1,  ROOT_CONSTANT_SPACE); 
ConstantBuffer< DescriptorHeapIndex > g_SPDCounter : register(b2,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV0    : register(b3,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV1    : register(b4,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV2    : register(b5,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV3    : register(b6,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV4    : register(b7,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV5    : register(b8,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV6    : register(b9,  ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV7    : register(b10, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV8    : register(b11, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV9    : register(b12, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV10   : register(b13, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV11   : register(b14, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_MipUAV12   : register(b15, ROOT_CONSTANT_SPACE);

// ============================================================================
// Reusable SPD layer (LDS + 5 common callbacks + FidelityFX type aliases)
// ============================================================================
#define SPD_SINGLE_CHANNEL
#include "SpdCommon.hlsli"

// ============================================================================
// Helper: write to a Hi-Z mip UAV by index
// ============================================================================
void SpdStoreHiZMip(ASU2 p, float value, uint hiZMip)
{
    RWTexture2D< float > dst;
    switch (hiZMip)
    {
        case  1: dst = GetResource(g_MipUAV1.index);  break;
        case  2: dst = GetResource(g_MipUAV2.index);  break;
        case  3: dst = GetResource(g_MipUAV3.index);  break;
        case  4: dst = GetResource(g_MipUAV4.index);  break;
        case  5: dst = GetResource(g_MipUAV5.index);  break;
        case  7: dst = GetResource(g_MipUAV7.index);  break;
        case  8: dst = GetResource(g_MipUAV8.index);  break;
        case  9: dst = GetResource(g_MipUAV9.index);  break;
        case 10: dst = GetResource(g_MipUAV10.index); break;
        case 11: dst = GetResource(g_MipUAV11.index); break;
        case 12: dst = GetResource(g_MipUAV12.index); break;
        default: return;
    }
    dst[p] = value;
}

// ============================================================================
// Per-effect callback: load source texel (Hi-Z mip 0).
// SPD's "source" is Hi-Z mip 0 — not the depth buffer — so that every SPD
// reduction step is an exact 2x2. Mip 0 was populated by CopyDepthToMip0
// earlier in main() via a scaled, min-reduction sample of the depth buffer.
// ============================================================================
AF4 SpdLoadSourceImage(ASU2 p, AU1 slice)
{
    RWTexture2D< float > mip0 = GetResource(g_MipUAV0.index);
    return AF4(mip0[p], 0, 0, 0);
}

// ============================================================================
// Per-effect callback: load from globally coherent mip (SPD mip 5 = Hi-Z mip 6)
// Used by the last workgroup in the global phase.
// ============================================================================
AF4 SpdLoad(ASU2 p, AU1 slice)
{
    globallycoherent RWTexture2D< float > mip6 = GetResource(g_MipUAV6.index);
    return AF4(mip6[p], 0, 0, 0);
}

// ============================================================================
// Per-effect callback: store to output mip
//   SPD mip N -> Hi-Z mip N+1
//   SPD mip 5 (Hi-Z mip 6) uses globallycoherent for cross-workgroup visibility.
// ============================================================================
void SpdStore(ASU2 p, AF4 value, AU1 mip, AU1 slice)
{
    if (mip == 5)
    {
        // Globally coherent write -- the last workgroup reads this via SpdLoad()
        globallycoherent RWTexture2D< float > dst = GetResource(g_MipUAV6.index);
        dst[p] = value.x;
    }
    else
    {
        SpdStoreHiZMip(p, value.x, mip + 1);
    }
}

// ============================================================================
// Per-effect callback: 2x2 min reduction for reversed-Z Hi-Z
// ============================================================================
AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)
{
    return AF4(min(min(v0.x, v1.x), min(v2.x, v3.x)), 0, 0, 0);
}

// ============================================================================
// Include the SPD algorithm (all 9 callbacks are now defined)
// ============================================================================
#include "../FidelityFX/ffx_spd.h"

void CopyDepthToMip0(uint2 workGroupID, uint localIndex)
{
    Texture2D< float >   depthSrc = GetResource(g_DepthSrc.index);
    RWTexture2D< float > mip0     = GetResource(g_MipUAV0.index);

    uint2 tileBase  = workGroupID * 64;
    uint  lx        = localIndex % 16;
    uint  ly        = localIndex / 16;
    uint2 blockBase = tileBase + uint2(lx * 4, ly * 4);

    float2 invMip0 = float2(1.0 / float(g_Mip0Width), 1.0 / float(g_Mip0Height));

    [unroll]
    for (uint dy = 0; dy < 4; dy++)
    {
        [unroll]
        for (uint dx = 0; dx < 4; dx++)
        {
            uint2 coord = blockBase + uint2(dx, dy);
            if (coord.x < g_Mip0Width && coord.y < g_Mip0Height)
            {
                float2 uv = (float2(coord) + 0.5) * invMip0;

                // LINEAR+MIN sampler returns min of the 2x2 bilinear footprint,
                // conservatively covering the <=2:1 depth-to-HiZ ratio (niagara-style).
                mip0[coord] = depthSrc.SampleLevel(g_LinearClampMinSampler, uv, 0);
            }
        }
    }
}


[numthreads(256, 1, 1)]
void main(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
    // Step 1: Copy Hi-Z mip 0 from the depth buffer.
    CopyDepthToMip0(WorkGroupId.xy, LocalThreadIndex);

    // Ensure mip 0 UAV writes are visible to SPD's source reads in this workgroup.
    DeviceMemoryBarrierWithGroupSync();

    // Step 2: SPD generates Hi-Z mips 1+ from mip 0 (single pass, exact 2x2 reductions).
    SpdDownsample(
        AU2(WorkGroupId.xy),
        AU1(LocalThreadIndex),
        AU1(g_SpdMipCount),
        AU1(g_NumWorkGroups),
        AU1(0)  // slice = 0 (Texture2D, not array)
    );
}
