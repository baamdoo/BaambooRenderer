#include "ShaderShared/DebugDraw.h"
#include "ShaderShared/Scene.h"
#include "Common/Binding.slangh"
#include "Common/DebugDraw.slangh"

struct DebugLinePush
{
    uint ID_Scene;
    uint ID_DebugLines;
};

[[vk::push_constant]] ConstantBuffer< DebugLinePush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

[shader("vertex")]
DebugVertexOutput main(uint vertexID : SV_VertexID)
{
    StructuredBuffer< SceneGlobals >    scene = GetResource(g_Push.ID_Scene);
    StructuredBuffer< DebugLineVertex > lines = GetResource(g_Push.ID_DebugLines);
    const DebugLineVertex vertex = lines[vertexID];

    DebugVertexOutput output;
    output.position = mul(scene[0].camera.mViewProj, float4(vertex.position, 1.0));
    output.color    = float4(vertex.color, vertex.alpha);
    return output;
}
