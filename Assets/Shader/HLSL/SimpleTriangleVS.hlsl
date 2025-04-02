#include "../Common.bsh"

static float2 positions[3] = {
    float2(0.0, 0.5),
    float2(0.5, -0.5),
    float2(-0.5, -0.5)
};

struct VSOutput
{
    float4 posCLIP : SV_POSITION;
};

VSOutput main(uint vid : SV_VertexID) {
    VSOutput OUT = (VSOutput)0;

    OUT.posCLIP = float4(positions[vid], 0.0, 1.0);
    return OUT;
}