#include "TerrainCommon.hlsli"
#include "VisibilityBuffer.hlsli"

struct PSOutput
{
    uint   VBuf0    : SV_Target0;  // visibility surface ID — terrain sentinel
    uint   VBuf1    : SV_Target1;  // unused for terrain
    float2 Velocity : SV_Target2;  // terrain placeholder = 0
};

PSOutput main(MSOutput IN)
{
    PSOutput output = (PSOutput)0;

    output.VBuf0    = VISID_TERRAIN;
    output.VBuf1    = 0u;
    output.Velocity = float2(0.0, 0.0); // static-terrain placeholder

    return output;
}
