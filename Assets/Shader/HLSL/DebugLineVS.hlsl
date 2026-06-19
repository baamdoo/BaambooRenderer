#define _CAMERA
#include "Common.hlsli"

struct DebugLineVertex
{
    float3 position;
    float  pad0;
    float3 color;
    float  alpha;
};

ConstantBuffer< DescriptorHeapIndex > g_DebugLines : register(b1, ROOT_CONSTANT_SPACE);

struct VSOut
{
    float4 position : SV_Position;
    float4 color    : TEXCOORD0;
};

VSOut main(uint vertID : SV_VertexID)
{
    StructuredBuffer< DebugLineVertex > lines = GetResource(g_DebugLines.index);
    DebugLineVertex vertex = lines[vertID];

    VSOut o = (VSOut)0;
    o.position = mul(g_Camera.mViewProj, float4(vertex.position, 1.0));
    o.color = float4(vertex.color, vertex.alpha);
    return o;
}
