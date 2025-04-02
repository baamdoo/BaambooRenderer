#include "../Common.bsh"

struct PSInput
{
    float4 posCLIP : SV_POSITION;
};

float4 main(PSInput IN) : SV_Target
{
    return float4(0.0, 0.0, 1.0, 1.0);
}