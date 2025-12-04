#define _SCENEENVIRONMENT
#include "AtmosphereCommon.hlsli"

struct PushConstants
{
    uint isoSampleCount;
    uint numRaySteps;
};
ConstantBuffer< PushConstants > g_Push : register(b0, ROOT_CONSTANT_SPACE);

ConstantBuffer< DescriptorHeapIndex > g_TransmittanceLUT      : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutMultiScatteringLUT : register(b2, ROOT_CONSTANT_SPACE);


float3 ComputeMultiScattering(float viewHeight, float cosZenithAngle)
{
    AtmosphereData Atmosphere = GetAtmosphereData();

    Texture2D< float3 > TransmittanceLUT = GetResource(g_TransmittanceLUT.index);


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
        float2 planetIntersection     = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, Atmosphere.planetRadiusKm);
        float2 atmosphereIntersection = RaySphereIntersection(rayOrigin, rayDir, PLANET_CENTER, Atmosphere.atmosphereRadiusKm);
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
            if (sampleHeight < Atmosphere.planetRadiusKm)
                break;
            
            float sampleAltitude = sampleHeight - Atmosphere.planetRadiusKm;

            // extinction(out-scattering) at sample point
            float rayleighDensity = GetDensityAtHeight(sampleAltitude, Atmosphere.rayleighDensityKm);
            float mieDensity      = GetDensityAtHeight(sampleAltitude, Atmosphere.mieDensityKm);
            float ozoneDensity    = GetDensityOzoneAtHeight(sampleAltitude, Atmosphere.ozoneCenterKm, Atmosphere.ozoneWidthKm);
            
            float3 rayleighScattering = Atmosphere.rayleighScattering * rayleighDensity;
            float  mieScattering      = Atmosphere.mieScattering * mieDensity;
            float  mieAbsorption      = Atmosphere.mieAbsorption * mieDensity;
            float3 ozoneAbsorption    = Atmosphere.ozoneAbsorption * ozoneDensity;

            float3 scattering        = rayleighScattering + mieScattering;
            float3 extinction        = rayleighScattering + (mieScattering + mieAbsorption) + ozoneAbsorption;
            float3 stepTransmittance = exp(-extinction * stepSize); // (2) T(xa,xb) = exp(−Integral(xa~xb, σ(x)dx))

            // transmittance from sample point to sun
            float  sampleTheta        = dot(normalize(pos), sunDir);
            float3 transmittanceToSun = SampleTransmittanceLUT(TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, Atmosphere.planetRadiusKm, Atmosphere.atmosphereRadiusKm);

            // planet shadow
            float2 planetIntersection = RaySphereIntersection(pos, rayDir, PLANET_CENTER, Atmosphere.planetRadiusKm);
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
            float3 transmittanceToSun = SampleTransmittanceLUT(TransmittanceLUT, g_LinearClampSampler, sampleHeight, sampleTheta, Atmosphere.planetRadiusKm, Atmosphere.atmosphereRadiusKm);

            // L'(x,v) = T(x,p) * Lo(p,v), where Lo : diffuse response according to ground albedo
	    	L += throughput * transmittanceToSun * NoL * float3(0.40981, 0.40981, 0.40981) / PI;
	    	// L += transmittanceToSun * throughput * NoL * Atmosphere.groundAlbedo / PI;
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
    RWTexture2D< float3 > OutMultiScatteringLUT = GetResource(g_OutMultiScatteringLUT.index);

    uint2 texSize;
    OutMultiScatteringLUT.GetDimensions(texSize.x, texSize.y);
    if (tID.x >= texSize.x || tID.y >= texSize.y)
        return;

    float2 uv = float2(tID.xy + 0.5) / float2(texSize);

    AtmosphereData Atmosphere = GetAtmosphereData();

    float cosZenithAngle = 2.0 * uv.x - 1.0;
    float viewHeight     = lerp(Atmosphere.planetRadiusKm, Atmosphere.atmosphereRadiusKm, uv.y);
    
    float3 multiScattering = ComputeMultiScattering(viewHeight, cosZenithAngle);
    
    OutMultiScatteringLUT[tID.xy] = multiScattering;
}