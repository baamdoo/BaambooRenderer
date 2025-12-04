#define _SCENEENVIRONMENT
#include "AtmosphereCommon.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_OutTransmittanceLUT : register(b1, ROOT_CONSTANT_SPACE);


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
    AtmosphereData Atmosphere = GetAtmosphereData();

    float stepSize = rayLength / float(numSamples);
    float3 extinction = float3(0.0, 0.0, 0.0);

    for (int i = 0; i < numSamples; ++i)
    {
        float t = (float(i) + 0.5) * stepSize;
        float3 pos = rayOrigin + t * rayDir;

        float altitude = GetAltitude(pos, Atmosphere.planetRadiusKm);

        float rayleighDensity = GetDensityAtHeight(altitude, Atmosphere.rayleighDensityKm);
        extinction += Atmosphere.rayleighScattering * rayleighDensity * stepSize;

        float mieDensity = GetDensityAtHeight(altitude, Atmosphere.mieDensityKm);
        extinction += (Atmosphere.mieScattering + Atmosphere.mieAbsorption) * mieDensity * stepSize;

        float ozoneDensity = GetDensityOzoneAtHeight(altitude, Atmosphere.ozoneCenterKm, Atmosphere.ozoneWidthKm);
        extinction += Atmosphere.ozoneAbsorption * ozoneDensity * stepSize;
    }

    return extinction;
}

[numthreads(8, 8, 1)]
void main(uint2 tID : SV_DispatchThreadID)
{
    RWTexture2D< float3 > OutTransmittanceLUT = GetResource(g_OutTransmittanceLUT.index);

    uint2 texSize;
    OutTransmittanceLUT.GetDimensions(texSize.x, texSize.y);
    if (tID.x >= texSize.x || tID.y >= texSize.y)
        return;

    float2 uv = float2(tID.xy + 0.5) / float2(texSize);

    AtmosphereData Atmosphere = GetAtmosphereData();

    float cosZenithAngle, viewHeight;
    GenerateTransmittanceCoordsFromUV(
        uv, Atmosphere.planetRadiusKm, Atmosphere.atmosphereRadiusKm, cosZenithAngle, viewHeight
    );
    
    float3 rayOrigin = float3(0.0, viewHeight, 0.0);
    float3 rayDir    = float3(safeSqrt(1.0 - cosZenithAngle * cosZenithAngle), cosZenithAngle, 0.0);
    
    float2 groundIntersection     = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, Atmosphere.planetRadiusKm);
    float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, Atmosphere.atmosphereRadiusKm);
    
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
        OutTransmittanceLUT[tID.xy] = float3(0.0, 0.0, 0.0);
        return;
    }
    
    const int numSamples = 64;
    float3 extinction    = ComputeExtinction(rayOrigin, rayDir, rayLength, numSamples);
    float3 transmittance = exp(-extinction); // (2) T(xa,xb) = exp(−Integral(xa~xb, σ(x)dx))
    
    OutTransmittanceLUT[tID.xy] = transmittance;
}