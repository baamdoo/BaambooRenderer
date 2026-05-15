#include "Common.hlsli"


struct PSIn
{
    float4 position : SV_Position;
    float4 color    : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target
{
    return i.color;
}
