#ifndef _HLSL_LIGHTING_HEADER
#define _HLSL_LIGHTING_HEADER

#include "Common.hlsli"
#include "HelperFunctions.hlsli"

// =============================================================================
// LTC LUT layout (Heitz 2016, selfshadow baked DDS)
// =============================================================================
static const float LTC_LUT_SIZE  = 64.0;
static const float LTC_LUT_SCALE = (LTC_LUT_SIZE - 1.0) / LTC_LUT_SIZE;
static const float LTC_LUT_BIAS  = 0.5 / LTC_LUT_SIZE;

// =============================================================================
// BRDF Building Blocks (Cook-Torrance GGX)
// =============================================================================
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a    = roughness * roughness;
    float a2   = a * a;
    float NoH  = max(dot(N, H), 0.0);
    float NoH2 = NoH * NoH;

    return a2 / (PI * pow(NoH2 * (a2 - 1.0) + 1.0, 2.0));
}

float GeometrySchlickGGX(float NoV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NoV / (NoV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0);
    return GeometrySchlickGGX(NoL, roughness) * GeometrySchlickGGX(NoV, roughness);
}

float3 CalculateBRDF(float3 N, float3 V, float3 L, float metallic, float roughness, float3 F0, inout float3 kD)
{
    float3 H = normalize(V + L);

    float  D = DistributionGGX(N, H, roughness);
    float  G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 kS = F;
    kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metallic;

    float3 numerator   = D * G * F;
    float  denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + EPSILON_MIN;

    return numerator / denominator;
}

// =============================================================================
// View-aligned TBN (LTC entry frame)
// =============================================================================
float3x3 BuildViewAlignedTBN(float3 N, float3 V, out float NoV)
{
    NoV = saturate(dot(N, -V));

    float3 surfaceX = -V - N * NoV;
    float  lenSq    = dot(surfaceX, surfaceX);
    if (lenSq < 1e-6)
    {
        float3 axis = (abs(N.y) < 0.9) ? float3(0, 1, 0) : float3(1, 0, 0);
        surfaceX = normalize(axis - N * dot(axis, N));
    }
    else
    {
        surfaceX = surfaceX * rsqrt(lenSq);
    }
    float3 surfaceY = cross(N, surfaceX);

    return float3x3(surfaceX, surfaceY, N);
}

// =============================================================================
// LTC polygon integral helpers (Heitz 2016, selfshadow rational fit)
// =============================================================================
float2 LtcLutCoords(float NoV, float roughness)
{
    float u = roughness;
    float v = sqrt(1.0 - NoV);
    return float2(u, v) * LTC_LUT_SCALE + LTC_LUT_BIAS;
}

float4 SampleLTC(Texture2D< float4 > LTC, float NoV, float roughness)
{
    return LTC.SampleLevel(g_LinearClampSampler, LtcLutCoords(NoV, roughness), 0);
}

float3 IntegrateEdgeVec(float3 v1, float3 v2)
{
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float theta_sintheta = (x > 0.0) ? v : 0.5 * rsqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2) * theta_sintheta;
}

float IntegrateEdge(float3 v1, float3 v2)
{
    return IntegrateEdgeVec(v1, v2).z;
}

void ClipQuadToHorizon(inout float3 L[17], out int n)
{
    int config = 0;
    if (L[0].z > 0.0) config |= 1;
    if (L[1].z > 0.0) config |= 2;
    if (L[2].z > 0.0) config |= 4;
    if (L[3].z > 0.0) config |= 8;

    n = 0;
    if (config == 0) { /* below — n=0 */ }
    else if (config == 1) {
        n = 3;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 2) {
        n = 3;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 3) {
        n = 4;
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
        L[3] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 4) {
        n = 3;
        L[0] = -L[3].z * L[2] + L[2].z * L[3];
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
    }
    else if (config == 5) { n = 0; /* 0,2 above — impossible */ }
    else if (config == 6) {
        n = 4;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 7) {
        n = 5;
        L[4] = -L[3].z * L[0] + L[0].z * L[3];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 8) {
        n = 3;
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
        L[1] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = L[3];
    }
    else if (config == 9) {
        n = 4;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[2].z * L[3] + L[3].z * L[2];
    }
    else if (config == 10) { n = 0; /* 1,3 above — impossible */ }
    else if (config == 11) {
        n = 5;
        L[4] = L[3];
        L[3] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 12) {
        n = 4;
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
    }
    else if (config == 13) {
        n = 5;
        L[4] = L[3];
        L[3] = L[2];
        L[2] = -L[1].z * L[2] + L[2].z * L[1];
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
    }
    else if (config == 14) {
        n = 5;
        L[4] = -L[0].z * L[3] + L[3].z * L[0];
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
    }
    else { n = 4; }
}

// Sutherland-Hodgman variant — clip an arbitrary N-vertex polygon (N <= 16) to the upper hemisphere (z >= 0).
void ClipPolygonToHorizon(inout float3 L[17], int N, out int n)
{
    float3 out_buf[17];
    int    out_n = 0;

    [loop]
    for (int i = 0; i < N; ++i)
    {
        float3 v_a = L[i];
        float3 v_b = L[(i + 1) % N];
        bool   in_a = v_a.z > 0.0;
        bool   in_b = v_b.z > 0.0;

        if (in_a && in_b)
        {
            out_buf[out_n++] = v_b;
        }
        else if (in_a && !in_b)
        {
            float t = v_a.z / (v_a.z - v_b.z);
            out_buf[out_n++] = lerp(v_a, v_b, t);
        }
        else if (!in_a && in_b)
        {
            float t = v_a.z / (v_a.z - v_b.z);
            out_buf[out_n++] = lerp(v_a, v_b, t);
            out_buf[out_n++] = v_b;
        }
    }

    [loop]
    for (int j = 0; j < out_n; ++j)
        L[j] = out_buf[j];
    n = out_n;
}

// cosine integral for n-vertex polygon (n <= 17, reused by Phase 2 quad and Phase 2.5 disk)
float PolygonRadiance(float3 P[17], int n)
{
    float sum = 0;
    [loop]
    for (int i = 0; i < n; ++i)
    {
        int j = (i + 1) % n;
        sum += IntegrateEdge(P[i], P[j]);
    }

    return max(0.0, sum * (1.0 / (2.0 * PI)));
}

bool ClipSegmentToHorizon(inout float3 v0, inout float3 v1)
{
    bool in0 = v0.z > 0.0;
    bool in1 = v1.z > 0.0;

    if (!in0 && !in1)
        return false;
    if (in0 && !in1) 
        v1 = lerp(v0, v1, v0.z / (v0.z - v1.z));
    if (!in0 && in1)
        v0 = lerp(v0, v1, v0.z / (v0.z - v1.z));

    return true;
}

// =============================================================================
// Light Apply (analytic — Directional / Spot / Sphere / Tube)
// =============================================================================
float3 ApplyDirectionalLight(DirectionalLight light, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    float3 L = normalize(float3(-light.dirX, -light.dirY, -light.dirZ));

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperatureK);

    float3 kD;
    float3 specular = CalculateBRDF(N, -V, L, metallic, roughness, F0, kD);

    float  NoL       = max(dot(N, L), 0.0);
    float3 luminance = lightColor * light.illuminanceLux;

    return (kD * albedo / PI + specular) * luminance * NoL;
}

float3 ApplySpotLight(SpotLight light, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    float3 L       = float3(light.posX, light.posY, light.posZ) - P;
    float distance = length(L);
    L             /= distance;

    // cone attenuation
    float cosTheta        = dot(L, normalize(float3(-light.dirX, -light.dirY, -light.dirZ)));
    float cosThetaInner   = cos(light.innerConeAngleRad);
    float cosThetaOuter   = cos(light.outerConeAngleRad);
    float spotAttenuation = clamp((cosTheta - cosThetaOuter) / (cosThetaInner - cosThetaOuter), 0.0, 1.0);

    if (spotAttenuation == 0.0)
        return float3(0.0, 0.0, 0.0);

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperatureK);

    float3 kD;
    float3 specular = CalculateBRDF(N, -V, L, metallic, roughness, F0, kD);

    float  NoL               = max(dot(N, L), 0.0);
    float  solidAngle        = PI_MUL(2.0) * (1.0 - cosThetaOuter);
    float  luminousIntensity = light.luminousFluxLm / solidAngle;
    float  attenuation       = 1.0 / max(distance * distance, light.radiusM * light.radiusM);
    float3 luminance         = lightColor * luminousIntensity * attenuation * spotAttenuation;

    return (kD * albedo / PI + specular) * luminance * NoL;
}

float3 ApplySphereLight(SphereLight light, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
	float3 L = float3(light.posX, light.posY, light.posZ) - P;
	float  d = length(L);

	float3 R = reflect(V, N);

	float  LoR         = dot(L, R);
	float3 centerToRay = LoR * R - L;

	float3 closestPt         = L + centerToRay * saturate(light.radius / max(length(centerToRay), 1e-6));
    float3 representativeRay = normalize(closestPt);

	float originalAlpha = roughness * roughness;
	float modifiedAlpha = saturate(originalAlpha + light.radius / (2.0 * d));
    float jacobian      = (originalAlpha * originalAlpha) / max(modifiedAlpha * modifiedAlpha, 1e-6);

    float3 kD;
    float3 specular = CalculateBRDF(N, -V, representativeRay, metallic, sqrt(modifiedAlpha), F0, kD) * jacobian;

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperatureK);

	float I           = light.luminousFluxLm / PI_MUL(4.0);
	float attenuation = 1.0 / max(d * d, light.radius * light.radius);

    float NoL = max(dot(N, L / d), 0.0);

    float3 luminance = lightColor * I * attenuation;
    return (kD * albedo / PI + specular) * luminance * NoL;
}

float3 ApplyTubeLight(
    TubeLight light, float3 P, float3 N, float3 V,
    float3 albedo, float metallic, float roughness, float3 F0)
{
    float3 P0 = float3(light.posAX, light.posAY, light.posAZ);
    float3 P1 = float3(light.posBX, light.posBY, light.posBZ);

    float3 R = reflect(V, N);

    float3 L0 = P0 - P;
    float3 L1 = P1 - P;
    float3 Ld = L1 - L0;

    float Ld2 = dot(Ld, Ld);

    float  tSpec     = (dot(R, L0) * dot(R, Ld) - dot(L0, Ld)) / max(Ld2 - dot(R, Ld) * dot(R, Ld), 1e-6);
    float3 closestPt = L0 + saturate(tSpec) * Ld;

    float3 centerToRay = dot(closestPt, R) * R - closestPt;
    float3 Lspec       = closestPt + centerToRay * saturate(light.radius / max(length(centerToRay), 1e-5));

    float dSpec = length(Lspec);

    float originalAlpha = roughness * roughness;
    float modifiedAlpha = saturate(originalAlpha + light.radius / (2.0 * dSpec));
    float jacobian      = (originalAlpha * originalAlpha) / max(modifiedAlpha * modifiedAlpha, 1e-6);

    float3 kD;
    float3 specular = CalculateBRDF(N, -V, Lspec / max(dSpec, 1e-6), metallic, sqrt(modifiedAlpha), F0, kD) * jacobian;

    float  tDiff = saturate(dot(-L0, Ld) / max(Ld2, 1e-6));
    float3 Ldiff = L0 + tDiff * Ld;

    float dDiff = length(Ldiff);
    float NoL   = max(dot(N, Ldiff / max(dDiff, 1e-6)), 0.0);

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperatureK);
    
    float I           = light.luminousFluxLm / PI_MUL(4.0);
    float attenuation = 1.0 / max(dDiff * dDiff, light.radius * light.radius);

    float3 luminance = lightColor * I * attenuation;
    return (kD * albedo / PI + specular) * luminance * NoL;
}

// =============================================================================
// Light Apply (LTC [area, disk, tube] — Heitz 2016)
// Reference: https://github.com/selfshadow/ltc_code
// =============================================================================
float3 ApplyAreaLight(
    AreaLight light, float3 P, float3 N, float3 V,
    float3 albedo, float metallic, float roughness, float3 F0,
    Texture2D< float4 > ltcMatrix, Texture2D< float4 > ltcAmplitude)
{
	float3 Snormal = float3(light.normalX, light.normalY, light.normalZ);
    if (dot(Snormal, P - float3(light.posX, light.posY, light.posZ)) > 0.0)
        return float3(0.0, 0.0, 0.0);

	float3 Stangent   = float3(light.tangentX, light.tangentY, light.tangentZ);
    float3 Sbitangent = cross(Snormal, Stangent);

	float3 center = float3(light.posX, light.posY, light.posZ);
    float3 corner[4] = {
        center - Stangent * light.halfWidth - Sbitangent * light.halfHeight, // bottom-left
        center + Stangent * light.halfWidth - Sbitangent * light.halfHeight, // bottom-right
        center + Stangent * light.halfWidth + Sbitangent * light.halfHeight, // top-right
        center - Stangent * light.halfWidth + Sbitangent * light.halfHeight  // top-left
	};

    float NoV;
    float3x3 mWorldToLTC = BuildViewAlignedTBN(N, V, NoV);

	float4 LTCcomponents = SampleLTC(ltcMatrix, NoV, roughness);
    float3x3 mLtcToFit = float3x3(
        LTCcomponents.x, 0.0, LTCcomponents.z,
        0.0,             1.0, 0.0,
		LTCcomponents.y, 0.0, LTCcomponents.w);

	float3x3 M = mul(mLtcToFit, mWorldToLTC);

    float3 Pspec[17];
    float3 Pdiff[17];
    for (int i = 0; i < 4; ++i)
    {
        Pspec[i] = normalize(mul(M, corner[i] - P));
        Pdiff[i] = normalize(mul(mWorldToLTC, corner[i] - P));
    }

    int Nspec, Ndiff;
    ClipQuadToHorizon(Pspec, Nspec);
    ClipQuadToHorizon(Pdiff, Ndiff);

    float Espec = PolygonRadiance(Pspec, Nspec);
    float Ediff = PolygonRadiance(Pdiff, Ndiff);

    float4 amplitude = SampleLTC(ltcAmplitude, NoV, roughness);
    float3 F         = F0 * amplitude.x + (1.0 - F0) * amplitude.y;
	float3 specular  = F * Espec;

    float3 kD      = (1 - F0) * (1 - metallic);
    float3 diffuse = kD * albedo * Ediff;

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperatureK);

	float  area      = (2.0 * light.halfWidth) * (2.0 * light.halfHeight);
    float3 luminance = lightColor * light.luminousFluxLm / (PI * area);

    return (diffuse + specular) * luminance;
}

#define DISK_NUM_SAMPLES 16
float3 ApplyDiskLight(
    DiskLight light, float3 P, float3 N, float3 V,
    float3 albedo, float metallic, float roughness, float3 F0,
    Texture2D< float4 > ltcMatrix, Texture2D< float4 > ltcAmplitude)
{
    float3 Snormal = float3(light.normalX, light.normalY, light.normalZ);
    float3 center  = float3(light.posX, light.posY, light.posZ);
    if (dot(Snormal, P - center) > 0.0)
        return float3(0.0, 0.0, 0.0);

    // Disk plane basis: tangent (azimuth zero) + bitangent.
    float3 Stangent   = float3(light.tangentX, light.tangentY, light.tangentZ);
    float3 Sbitangent = cross(Snormal, Stangent);

    float NoV;
    float3x3 mWorldToLTC = BuildViewAlignedTBN(N, V, NoV);

    float4 LTCcomponents = SampleLTC(ltcMatrix, NoV, roughness);
    float3x3 mLtcToFit = float3x3(
        LTCcomponents.x, 0.0, LTCcomponents.z,
        0.0,             1.0, 0.0,
        LTCcomponents.y, 0.0, LTCcomponents.w);
    float3x3 M = mul(mLtcToFit, mWorldToLTC);
    
    float3 Pspec[DISK_NUM_SAMPLES + 1];
    float3 Pdiff[DISK_NUM_SAMPLES + 1];
    for (int i = 0; i < DISK_NUM_SAMPLES; ++i)
    {
        float  theta  = 2.0 * PI * i / DISK_NUM_SAMPLES;
        float3 corner = center + light.radius * (cos(theta) * Stangent + sin(theta) * Sbitangent);

        Pspec[i] = normalize(mul(M, corner - P));
        Pdiff[i] = normalize(mul(mWorldToLTC, corner - P));
    }

    int Nspec, Ndiff;
    ClipPolygonToHorizon(Pspec, DISK_NUM_SAMPLES, Nspec);
    ClipPolygonToHorizon(Pdiff, DISK_NUM_SAMPLES, Ndiff);

    float Espec = PolygonRadiance(Pspec, Nspec);
    float Ediff = PolygonRadiance(Pdiff, Ndiff);

    float4 amplitude = SampleLTC(ltcAmplitude, NoV, roughness);
    float3 F         = F0 * amplitude.x + (1.0 - F0) * amplitude.y;
    float3 specular  = F * Espec;

    float3 kD      = (1 - F0) * (1 - metallic);
    float3 diffuse = kD * albedo * Ediff;

    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    if (light.temperatureK > 0.0)
        lightColor *= ColorTemperatureToRGB(light.temperatureK);

    float  area      = PI * light.radius * light.radius;
    float3 luminance = lightColor * light.luminousFluxLm / (PI * area);

    return (diffuse + specular) * luminance;
}

#endif // _HLSL_LIGHTING_HEADER
