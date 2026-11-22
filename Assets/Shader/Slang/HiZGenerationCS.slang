#include "ShaderShared/Culling.h"
#include "Common/Binding.slangh"

struct HiZPush
{
    HiZConstants constants;

    uint ID_DepthSrc;
    uint ID_MipUAV0;
    uint ID_MipUAV1;
    uint ID_MipUAV2;
    uint ID_MipUAV3;
    uint ID_MipUAV4;
    uint ID_MipUAV5;
    uint ID_MipUAV6;
    uint ID_MipUAV7;
    uint ID_MipUAV8;
    uint ID_MipUAV9;
    uint ID_MipUAV10;
    uint ID_MipUAV11;
    uint ID_MipUAV12;
};

[[vk::push_constant]] ConstantBuffer< HiZPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

uint MipUAV(uint hiZMip)
{
    switch (hiZMip)
    {
        case  0: return g_Push.ID_MipUAV0;
        case  1: return g_Push.ID_MipUAV1;
        case  2: return g_Push.ID_MipUAV2;
        case  3: return g_Push.ID_MipUAV3;
        case  4: return g_Push.ID_MipUAV4;
        case  5: return g_Push.ID_MipUAV5;
        case  6: return g_Push.ID_MipUAV6;
        case  7: return g_Push.ID_MipUAV7;
        case  8: return g_Push.ID_MipUAV8;
        case  9: return g_Push.ID_MipUAV9;
        case 10: return g_Push.ID_MipUAV10;
        case 11: return g_Push.ID_MipUAV11;
        default: return g_Push.ID_MipUAV12;
    }
}

uint2 MipSize(uint hiZMip)
{
    return max(uint2(g_Push.constants.mip0Width, g_Push.constants.mip0Height) >> hiZMip, 1u);
}

#define A_GPU
#define A_HLSL
#include "../FidelityFX/ffx_a.h"

groupshared AF1 spdIntermediateR[16][16];

AF4  SpdLoadIntermediate(AU1 x, AU1 y)             { return AF4(spdIntermediateR[x][y], 0, 0, 0); }
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value) { spdIntermediateR[x][y] = value.x; }

// SPD's cross-group handshake is never reached: the mips past 6 run as a second dispatch
void SpdIncreaseAtomicCounter(AU1 slice) {}
AU1  SpdGetAtomicCounter()               { return 0u; }
void SpdResetAtomicCounter(AU1 slice)    {}

// Out-of-bounds texels read as 1.0 (near), the identity of min, so they never pull a reduction toward far
float LoadMip(uint hiZMip, int2 p)
{
    if (any(uint2(p) >= MipSize(hiZMip)))
        return 1.0;

    RWTexture2D< float > mip = GetResource(MipUAV(hiZMip));
    return mip[p];
}

AF4 SpdLoadSourceImage(ASU2 p, AU1 slice) { return AF4(LoadMip(0u, p), 0, 0, 0); }
AF4 SpdLoad(ASU2 p, AU1 slice)            { return AF4(LoadMip(6u, p), 0, 0, 0); }

// SPD mip N is HiZ mip N + 1
void SpdStore(ASU2 p, AF4 value, AU1 mip, AU1 slice)
{
    const uint hiZMip = mip + 1u;
    if (any(uint2(p) >= MipSize(hiZMip)))
        return;

    RWTexture2D< float > dst = GetResource(MipUAV(hiZMip));
    dst[p] = value.x;
}

AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)
{
    return AF4(min(min(v0.x, v1.x), min(v2.x, v3.x)), 0, 0, 0);
}

#include "../FidelityFX/ffx_spd.h"

void CopyDepthToMip0(uint2 workGroupID, uint localIndex)
{
    Texture2D< float >   depthSrc = GetResource(g_Push.ID_DepthSrc);
    RWTexture2D< float > mip0     = GetResource(g_Push.ID_MipUAV0);

    const uint2  mip0Size  = MipSize(0u);
    const uint2  blockBase = workGroupID * 64u + uint2(localIndex % 16u, localIndex / 16u) * 4u;
    const float2 invMip0   = 1.0 / float2(mip0Size);

    [unroll]
    for (uint dy = 0; dy < 4; ++dy)
    {
        [unroll]
        for (uint dx = 0; dx < 4; ++dx)
        {
            const uint2 coord = blockBase + uint2(dx, dy);
            if (all(coord < mip0Size))
            {
                // LINEAR+MIN returns the min of the 2x2 bilinear taps, which covers a depth-to-HiZ ratio below 2
                mip0[coord] = depthSrc.SampleLevel(g_LinearClampMinSampler, (float2(coord) + 0.5) * invMip0, 0);
            }
        }
    }
}

uint2 SpdThreadXY(uint localIndex)
{
    const AU2 sub = ARmpRed8x8(localIndex % 64u);
    return uint2(sub.x + 8u * ((localIndex >> 6) % 2u), sub.y + 8u * (localIndex >> 7));
}

[numthreads(256, 1, 1)]
void DownsampleTiles(uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex)
{
    CopyDepthToMip0(groupID.xy, localIndex);
    DeviceMemoryBarrierWithGroupSync();

    const uint  mips = min(g_Push.constants.spdMipCount, 6u);
    const uint2 xy   = SpdThreadXY(localIndex);
    SpdDownsampleMips_0_1(xy.x, xy.y, groupID.xy, localIndex, mips, 0u);
    SpdDownsampleNextFour(xy.x, xy.y, groupID.xy, localIndex, 2u, mips, 0u);
}

[numthreads(256, 1, 1)]
void DownsampleTail(uint localIndex : SV_GroupIndex)
{
    const uint  mips = g_Push.constants.spdMipCount;
    const uint2 xy   = SpdThreadXY(localIndex);
    SpdDownsampleMips_6_7(xy.x, xy.y, mips, 0u);
    SpdDownsampleNextFour(xy.x, xy.y, uint2(0, 0), localIndex, 8u, mips, 0u);
}
