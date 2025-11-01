#ifndef _HLSL_HELPER_FUNCTION_HEADER
#define _HLSL_HELPER_FUNCTION_HEADER

#define _HLSL
#include "../Common.bsh"

float inverseLerp(float v, float minValue, float maxValue) 
{
	return (v - minValue) / (maxValue - minValue);
}

float remap(float v, float inMin, float inMax, float outMin, float outMax) 
{
	float t = inverseLerp(v, inMin, inMax);
	return lerp(outMin, outMax, t);
}

float safeRemap(float value, float oldMin, float oldMax, float newMin, float newMax)
{
    return clamp((value - oldMin) / (oldMax - oldMin), 0.0, 1.0) * (newMax - newMin) + newMin;
}

float clampedRemap(float value, float oldMin, float oldMax, float newMin, float newMax)
{
    return clamp(remap(value, oldMin, oldMax, newMin, newMax), newMin, newMax);
}

float safeSqrt(float a)
{
    return sqrt(max(a, 0.0));
}

// Reference: https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Shaders/Private/FastMath.ush
float acosFast4(float inX)
{
    float x1 = abs(inX);
    float x2 = x1 * x1;
    float x3 = x2 * x1;
    float s;

    s = -0.2121144 * x1 + 1.5707288;
    s = 0.0742610 * x2 + s;
    s = -0.0187293 * x3 + s;
    s = sqrt(1.0 - x1) * s;

    return inX >= 0.0 ? s : PI - s;
}

float atan2Fast(float y, float x)
{
    float t0 = max(abs(x), abs(y));
    float t1 = min(abs(x), abs(y));
    float t3 = t1 / t0;
    float t4 = t3 * t3;

	// Same polynomial as atanFastPos
    t0 = +0.0872929;
    t0 = t0 * t4 - 0.301895;
    t0 = t0 * t4 + 1.0;
    t3 = t0 * t3;

    t3 = abs(y) > abs(x) ? (0.5 * PI) - t3 : t3;
    t3 = x < 0 ? PI - t3 : t3;
    t3 = y < 0 ? -t3 : t3;

    return t3;
}

float3 ReconstructWorldPos(float2 uv, float depth, float4x4 mViewProjInv)
{
    float4 posCLIP  = float4(uv.x * 2.0 - 1.0, uv.y * -2.0 + 1.0, depth, 1.0);
    // float4 posCLIP = float4(uv * 2.0 - 1.0, depth, 1.0);
    // float4 posWORLD = mViewProjInv * posCLIP;
    float4 posWORLD = mul(mViewProjInv, posCLIP);

    return posWORLD.xyz / posWORLD.w;
}

float LinearizeDepth(float depth, float near, float far)
{
    return (near * far) / (far - depth * (far - near));
}

float ConvertColorToLuminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

static const float3 COLOR_TEMPERATURE_LUT[] = 
{
    float3(1.000, 0.180, 0.000),  // 1000K
    float3(1.000, 0.390, 0.000),  // 2000K
    float3(1.000, 0.588, 0.275),  // 3000K
    float3(1.000, 0.707, 0.518),  // 4000K
    float3(1.000, 0.792, 0.681),  // 5000K
    float3(1.000, 0.849, 0.818),  // 6000K
    float3(0.949, 0.867, 1.000),  // 7000K
    float3(0.827, 0.808, 1.000),  // 8000K
    float3(0.765, 0.769, 1.000),  // 9000K
    float3(0.726, 0.742, 1.000), // 10000K
};

float3 ColorTemperatureToRGB(float temperature_K)
{
    float T = clamp(temperature_K, 1000.0, 10000.0);

    float index = (T - 1000.0) / 1000.0;
    return lerp(COLOR_TEMPERATURE_LUT[int(index)], 
               COLOR_TEMPERATURE_LUT[int(index) + 1], 
               frac(index));
}

float2 GetStretchedTextureUV(float2 uv, float2 resolution)
{
    return (uv - 0.5 / resolution) * resolution / (resolution - 1.0);
}

float2 GetUnstretchedTextureUV(float2 uv, float2 resolution)
{
    return (uv + 0.5 / resolution) * resolution / (resolution + 1.0);
}

float ConvertToViewDepth(float depth, float4 deviceZtoViewZ)
{
    return depth * deviceZtoViewZ[0] + deviceZtoViewZ[1] + 1.0f / (depth * deviceZtoViewZ[2] - deviceZtoViewZ[3]);
}

float2 RaySphereIntersection(float3 rayOrigin, float3 rayDir, float3 sphereCenter, float sphereRadius)
{
    float3 centerToOrigin = rayOrigin - sphereCenter;

    float a = dot(rayDir, rayDir);
    float b = 2.0 * dot(centerToOrigin, rayDir);
    float c = dot(centerToOrigin, centerToOrigin) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4.0 * a * c;
    
    if (discriminant < 0.0)
        return float2(-1.0, -1.0);
    
    float root0 = (-b - sqrt(discriminant)) / (2.0 * a);
    float root1 = (-b + sqrt(discriminant)) / (2.0 * a);
    return float2(root0, root1);
}

float3 RayPlaneIntersection(float3 rayOrigin, float3 rayDir, float4 plane)
{
    float t = (plane.a - dot(rayOrigin, plane.xyz)) / max(dot(rayDir, plane.xyz), EPSILON);
    return rayOrigin + t * rayDir;
}


// --- Temporal --- //
// Convert RGB to YCoCg color space for better neighborhood clamping
float3 RGB2YCoCg(float3 rgb)
{
    float Y  = dot(rgb, float3(0.25, 0.5, 0.25));
    float Co = dot(rgb, float3(0.5, 0.0, -0.5));
    float Cg = dot(rgb, float3(-0.25, 0.5, -0.25));
    return float3(Y, Co, Cg);
}

float3 YCoCg2RGB(float3 ycocg)
{
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;
    
    float R = Y + Co - Cg;
    float G = Y + Cg;
    float B = Y - Co - Cg;
    
    return float3(R, G, B);
}

// Reference: https://www.shadertoy.com/view/MtVGWz
float4 TextureCatmullRom(Texture2D tex, SamplerState smp, float2 uv, float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1   = floor(samplePos - 0.5) + 0.5;

    float2 f  = samplePos - texPos1;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12      = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    float2 texPos0  = texPos1 - 1.0;
    float2 texPos3  = texPos1 + 2.0;
    float2 texPos12 = texPos1 + offset12;

    texPos0  /= texSize;
    texPos3  /= texSize;
    texPos12 /= texSize;

    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    result += tex.SampleLevel(smp, float2(texPos0.x, texPos0.y), 0) * w0.x * w0.y;
    result += tex.SampleLevel(smp, float2(texPos12.x, texPos0.y), 0) * w12.x * w0.y;
    result += tex.SampleLevel(smp, float2(texPos3.x, texPos0.y), 0) * w3.x * w0.y;
    result += tex.SampleLevel(smp, float2(texPos0.x, texPos12.y), 0) * w0.x * w12.y;
    result += tex.SampleLevel(smp, float2(texPos12.x, texPos12.y), 0) * w12.x * w12.y;
    result += tex.SampleLevel(smp, float2(texPos3.x, texPos12.y), 0) * w3.x * w12.y;
    result += tex.SampleLevel(smp, float2(texPos0.x, texPos3.y), 0) * w0.x * w3.y;
    result += tex.SampleLevel(smp, float2(texPos12.x, texPos3.y), 0) * w12.x * w3.y;
    result += tex.SampleLevel(smp, float2(texPos3.x, texPos3.y), 0) * w3.x * w3.y;

    return result;
}

// Variance clipping for better ghosting reduction
float ClipAABB(float aabbMin, float aabbMax, float history)
{
    float center = 0.5 * (aabbMax + aabbMin);
    float extents = 0.5 * (aabbMax - aabbMin);

    float v_clip = history - center;
    
    if (extents < 1e-6) 
        return center;

    float v_unit = v_clip / extents;
    float a_unit = abs(v_unit);

    if (a_unit > 1.0)
        return center + v_clip / a_unit;
    else
        return history;
}
float3 ClipAABB(float3 aabbMin, float3 aabbMax, float3 history)
{
    float3 center  = 0.5 * (aabbMax + aabbMin);
    float3 extents = 0.5 * (aabbMax - aabbMin);

    float3 v_clip = history - center;
    float3 v_unit = v_clip / extents;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return center + v_clip / ma_unit;
    else
        return history;
}
// ---------------- //

#endif // _HLSL_HELPER_FUNCTION_HEADER