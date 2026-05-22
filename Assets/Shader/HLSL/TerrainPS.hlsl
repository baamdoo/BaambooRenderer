#include "TerrainCommon.hlsli"

float4 main(MSOutput IN) : SV_Target0
{
	float3 color = normalize(IN.normalWS) * 0.5 + 0.5;
    return float4(color, 1.0);
}
