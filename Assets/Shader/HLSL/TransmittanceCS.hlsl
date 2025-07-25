#define _ATMOSPHERE
#include "AtmosphereCommon.hlsli"

RWTexture2D< float3 > g_TransmittanceLUT : register(u0);

void GenerateTransmittanceCoordsFromUV(float2 uv, float bottomRadius, float topRadius, out float cosZenithAngle, out float viewHeight)
{
    // Put more frequencies near the horizon
    float H    = safeSqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    float rho  = H * uv.y;
    viewHeight = safeSqrt(rho * rho + bottomRadius * bottomRadius);

    float d_min = topRadius - viewHeight;
    float d_max = rho + H;
    float d     = lerp(d_min, d_max, uv.x);

    cosZenithAngle = d == 0.0 ?
        1.0 : (H * H - rho * rho - d * d) / (2.0 * viewHeight * d);
    cosZenithAngle = clamp(cosZenithAngle, -1.0, 1.0);
}

float3 ComputeExtinction(float3 rayOrigin, float3 rayDir, float rayLength, int numSamples)
{
    float stepSize = rayLength / float(numSamples);
    float3 extinction = float3(0.0, 0.0, 0.0);

    for (int i = 0; i < numSamples; ++i)
    {
        float t = (float(i) + 0.5) * stepSize;
        float3 pos = rayOrigin + t * rayDir;

        float altitude = GetAltitude(pos);

        float rayleighDensity = GetDensityAtHeight(altitude, g_Atmosphere.rayleighDensityH_km);
        extinction += g_Atmosphere.rayleighScattering * rayleighDensity * stepSize;

        float mieDensity = GetDensityAtHeight(altitude, g_Atmosphere.mieDensityH_km);
        extinction += (g_Atmosphere.mieScattering + g_Atmosphere.mieAbsorption) * mieDensity * stepSize;

        float ozoneDensity = GetDensityOzoneAtHeight(altitude);
        extinction += g_Atmosphere.ozoneAbsorption * ozoneDensity * stepSize;
    }

    return extinction;
}

[numthreads(8, 8, 1)]
void main(uint2 tID : SV_DispatchThreadID)
{
    uint2 texSize;
    g_TransmittanceLUT.GetDimensions(texSize.x, texSize.y);
    if (tID.x >= texSize.x || tID.y >= texSize.y)
        return;

    float2 uv = float2(tID.xy + 0.5) / float2(texSize);

    float cosZenithAngle, viewHeight;
    GenerateTransmittanceCoordsFromUV(
        uv, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km, cosZenithAngle, viewHeight
    );
    
    float3 rayOrigin = float3(0.0, viewHeight, 0.0);
    float3 rayDir    = float3(safeSqrt(1.0 - cosZenithAngle * cosZenithAngle), cosZenithAngle, 0.0);
    
    float2 groundIntersection     = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Atmosphere.planetRadius_km);
    float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Atmosphere.atmosphereRadius_km);
    
    float rayLength;
    if (groundIntersection.x > 0.0) 
    {
        // ray hits ground
        rayLength = groundIntersection.x;
    } else 
    {
        // ray goes through atmosphere
        rayLength = atmosphereIntersection.y;
    }
    
    if (rayLength <= 0.0) 
    {
        g_TransmittanceLUT[tID.xy] = float3(0.0, 0.0, 0.0);
        return;
    }
    
    const int numSamples = 64;
    float3 extinction    = ComputeExtinction(rayOrigin, rayDir, rayLength, numSamples);
    float3 transmittance = exp(-extinction); // (2) T(xa,xb) = exp(−Integral(xa~xb, σ(x)dx))
    
    g_TransmittanceLUT[tID.xy] = transmittance;
}