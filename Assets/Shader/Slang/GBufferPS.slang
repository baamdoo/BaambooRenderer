#include "Common/GBuffer.slangh"
#include "Common/VisibilityBuffer.slangh"

static const uint kBayerMatrix[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

// Slang rejects per-primitive markers on entry inputs; global inputs get PerPrimitiveEXT through spirv_asm.
// Any global input moves the bindless heap off set 0 if this shader also reads the heap, so it must not.
[[vk::location(0)]] nointerpolation in uint gVisID0;
[[vk::location(1)]] nointerpolation in uint gVisID1;
[[vk::location(2)]] nointerpolation in uint gFade;

void MarkPerPrimitiveInputs()
{
    __target_switch
    {
    case spirv:
        spirv_asm
        {
            OpCapability MeshShadingEXT;
            OpExtension "SPV_EXT_mesh_shader";
            OpDecorate &gVisID0 PerPrimitiveEXT;
            OpDecorate &gVisID1 PerPrimitiveEXT;
            OpDecorate &gFade PerPrimitiveEXT;
        };
    default:
        return;
    }
}

struct PSOutput
{
    uint VBuf0 : SV_Target0; // visibility surface ID
    uint VBuf1 : SV_Target1; // visibility primitive ID
};

[shader("pixel")]
PSOutput main(float4 position : SV_Position)
{
    MarkPerPrimitiveInputs();

    // Voxel LOD crossfade: the fading-in and fading-out chunks keep complementary Bayer cells
    if (VisIsVoxel(gVisID0))
    {
        const uint fadeThreshold = (gFade >> 8u) & 0x1Fu;
        const bool bFadeOut      = (gFade & 2u) != 0u;

        const uint bayer = kBayerMatrix[uint(position.x) & 3u][uint(position.y) & 3u];
        if (bFadeOut ? bayer < fadeThreshold : bayer >= fadeThreshold)
            discard;
    }

    PSOutput output;
    output.VBuf0 = gVisID0;
    output.VBuf1 = gVisID1;
    return output;
}
