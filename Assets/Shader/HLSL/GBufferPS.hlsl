#define _MATERIAL
#include "Common.hlsli"


struct PSInput
{
    float4 position     : SV_Position;
    float4 posCurrCLIP  : POSITION0;
    float4 posPrevCLIP  : POSITION1;

    nointerpolation uint visID0     : ID1;
    nointerpolation uint visID1     : ID2;
};

struct PSOutput
{
    uint   VBuf0    : SV_Target0;  // visibility surface ID
    uint   VBuf1    : SV_Target1;  // visibility primitive ID
	float2 Velocity : SV_Target2;  // currUV - prevUV (Screen-space motion vector)
};

PSOutput main(PSInput input)
{
    PSOutput output = (PSOutput)0;

    float2 posPrevUV = (input.posPrevCLIP.xy / input.posPrevCLIP.w) * 0.5 + 0.5;
    float2 posCurrUV = (input.posCurrCLIP.xy / input.posCurrCLIP.w) * 0.5 + 0.5;

    output.VBuf0    = input.visID0;
    output.VBuf1    = input.visID1;
    output.Velocity = posCurrUV - posPrevUV;
    return output;
}