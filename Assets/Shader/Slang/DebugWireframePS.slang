#include "Common/DebugDraw.slangh"

[shader("pixel")]
float4 main(DebugVertexOutput input) : SV_Target
{
    return input.color;
}
