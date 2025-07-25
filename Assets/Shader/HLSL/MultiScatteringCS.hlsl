#include "Common.hlsli"
#define _ATMOSPHERE
#include "AtmosphereCommon.hlsli"

Texture2D< float3 >   g_TransmittanceLUT   : register(t0);
RWTexture2D< float3 > g_MultiScatteringLUT : register(u0);

SamplerState g_LinearClampSampler : register(s0);

// Push constants
struct PushConstants
{
    uint isoSampleCount;
    uint numRaySteps;
};
ConstantBuffer< PushConstants > g_Push : register(b0, ROOT_CONSTANT_SPACE);

float3 ComputeMultiScattering(float viewHeight, float cosZenithAngle)
{
    float3 rayOrigin = float3(0.0, viewHeight, 0.0);
    float3 sunDir    = float3(safeSqrt(1.0 - cosZenithAngle * cosZenithAngle), cosZenithAngle, 0.0);
    
    float3 L2ndOrder = 0.0;
    float3 fms       = 0.0;
    
    float weight       = (4.0 * PI) / float(g_Push.isoSampleCount);
    float uniformPhase = 1.0 / (4.0 * PI);

    // uniform sphere sampling(isotropic phase function)
    const float goldenRatio = (1.0 + sqrt(5.0)) / 2.0;
    for (uint i = 0u; i < g_Push.isoSampleCount; ++i) 
    {
        float theta = 2.0 * PI * float(i) / goldenRatio;
        float phi   = acos(1.0 - 2.0 * float(i) / float(g_Push.isoSampleCount));
        
        float3 rayDir = float3(
            sin(phi) * cos(theta),
            cos(phi),
            sin(phi) * sin(theta)
        );
        
        // compute ray length
        float  rayLength              = 0.0;
        float2 planetIntersection     = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Atmosphere.planetRadius_km);
        float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, g_Atmosphere.atmosphereRadius_km);
        if (atmosphereIntersection.y < 0.0) 
            continue;
        else if (planetIntersection.x < 0.0)
            rayLength = atmosphereIntersection.y;
        else
            rayLength = planetIntersection.x;
        
        float stepSize = rayLength / float(g_Push.numRaySteps);
        
        float3 L          = 0.0;
        float3 Lf         = 0.0;
        float3 throughput = 1.0;
        for (int step = 0; step < g_Push.numRaySteps; ++step) 
        {
            float  t   = (float(step) + 0.5) * stepSize;
            float3 pos = rayOrigin + t * rayDir;

            // skip if below ground
            float sampleHeight = length(pos);
            if (sampleHeight < g_Atmosphere.planetRadius_km)
                break;
            
            float sampleAltitude = sampleHeight - g_Atmosphere.planetRadius_km;

            // extinction(out-scattering) at sample point
            float rayleighDensity = GetDensityAtHeight(sampleAltitude, g_Atmosphere.rayleighDensityH_km);
            float mieDensity      = GetDensityAtHeight(sampleAltitude, g_Atmosphere.mieDensityH_km);
            float ozoneDensity    = GetDensityOzoneAtHeight(sampleAltitude);
            
            float3 rayleighScattering = g_Atmosphere.rayleighScattering * rayleighDensity;
            float  mieScattering      = g_Atmosphere.mieScattering * mieDensity;
            float  mieAbsorption      = g_Atmosphere.mieAbsorption * mieDensity;
            float3 ozoneAbsorption    = g_Atmosphere.ozoneAbsorption * ozoneDensity;

            float3 scattering        = rayleighScattering + mieScattering;
            float3 extinction        = rayleighScattering + (mieScattering + mieAbsorption) + ozoneAbsorption;
            float3 stepTransmittance = exp(-extinction * stepSize); // (2) T(xa,xb) = exp(−Integral(xa~xb, σ(x)dx))

            // transmittance from sample point to sun
            float  sampleTheta        = dot(normalize(pos), sunDir);
            float3 transmittanceToSun = SampleTransmittanceLUT(g_TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km);

            // planet shadow
            float2 planetIntersection = RaySphereIntersection(pos, rayDir, PLANET_CENTER, g_Atmosphere.planetRadius_km);
            float  planetShadow       = planetIntersection.x < 0.0 ? 1.0 : 0.0;

            // (4) S(x,l) = Vis(l) * T(x,x+t*l)
            float3 S = planetShadow * transmittanceToSun;
            
            L          += scattering * throughput * S * uniformPhase * 1.0 * stepSize; // (6) L'(x,v) = @ + σs(x) * T(x,x-tv) * S(x,ws) * pu * EI * dt
            Lf         += scattering * throughput * 1.0 * stepSize;                    // (8) Lf(x,v) = σs(x) * T(x,x-tv) * 1 * dt
            throughput *= stepTransmittance;
        }

        // contribution of light scattered from the ground
        if (rayLength == planetIntersection.x)
	    {
            float3 pos = rayOrigin + planetIntersection.x * rayDir;

	    	float  sampleHeight = length(pos);
            float3 upVec      = pos / sampleHeight;
	    	float  NoL        = clamp(dot(upVec, sunDir), 0.0, 1.0);

	    	// transmittance from sample point to sun
            float  sampleTheta        = dot(upVec, sunDir);
            float3 transmittanceToSun = SampleTransmittanceLUT(g_TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km);

            // L'(x,v) = T(x,p) * Lo(p,v), where Lo : diffuse response according to ground albedo
	    	L += throughput * transmittanceToSun * NoL * float3(0.40981, 0.40981, 0.40981) / PI;
	    	// L += transmittanceToSun * throughput * NoL * g_Atmosphere.groundAlbedo / PI;
	    }
        
        // (5) L2ndOrder_i = L'(xs, -w) * pu * dw
        L2ndOrder += (L * uniformPhase) * weight;
        
        // (7) fms_i = Lf(xs, -w) * pu * dw
        fms += (Lf * uniformPhase) * weight;
    }
    
    // Compute infinite scattering series: 1 + f + f^2 + f^3 + ... = 1 / (1 - f)
    // This is based on the observation that after 2nd order, scattering becomes mostly isotropic
    return L2ndOrder / (1.0 - fms);
}

[numthreads(8, 8, 1)]
void main(uint2 tID : SV_DispatchThreadID)
{
    uint2 texSize;
    g_MultiScatteringLUT.GetDimensions(texSize.x, texSize.y);
    if (tID.x >= texSize.x || tID.y >= texSize.y)
        return;

    float2 uv = float2(tID.xy + 0.5) / float2(texSize);
    
    float cosZenithAngle = 2.0 * uv.x - 1.0;
    float viewHeight     = lerp(g_Atmosphere.planetRadius_km, g_Atmosphere.atmosphereRadius_km, uv.y);
    
    float3 multiScattering = ComputeMultiScattering(viewHeight, cosZenithAngle);
    
    g_MultiScatteringLUT[tID.xy] = multiScattering;
}