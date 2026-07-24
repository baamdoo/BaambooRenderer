#define _MATERIAL
#include "Common.hlsli"


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

    output.VBuf0 = input.visID0;
    output.VBuf1 = input.visID1;
    return output;
}
