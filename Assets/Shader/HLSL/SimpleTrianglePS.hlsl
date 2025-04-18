#include "../Common.bsh"

struct PSInput
{
    float4 posCLIP : SV_POSITION;
    float4 color : COLOR0;
};

float4 main(PSInput IN) : SV_Target
{
    return IN.color;
}