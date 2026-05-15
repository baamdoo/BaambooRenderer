#ifndef _HLSL_DEBUG_COLOR_HEADER
#define _HLSL_DEBUG_COLOR_HEADER

// =========================================================================
// Reference: https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f
// =========================================================================
float3 TurboColormap(float t)
{
    t = saturate(t);

    const float4 kRedVec4   = float4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    const float4 kGreenVec4 = float4(0.09140261, 2.19418839,   4.84296658, -14.18503333);
    const float4 kBlueVec4  = float4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    const float2 kRedVec2   = float2(-152.94239396, 59.28637943);
    const float2 kGreenVec2 = float2(  4.27729857,  2.82956604);
    const float2 kBlueVec2  = float2(-89.90310912, 27.34824973);

    float4 v4 = float4(1.0, t, t * t, t * t * t);
    float2 v2 = v4.zw * v4.z;

    return float3(
        dot(v4, kRedVec4)   + dot(v2, kRedVec2),
        dot(v4, kGreenVec4) + dot(v2, kGreenVec2),
        dot(v4, kBlueVec4)  + dot(v2, kBlueVec2));
}

#endif // _HLSL_DEBUG_COLOR_HEADER
