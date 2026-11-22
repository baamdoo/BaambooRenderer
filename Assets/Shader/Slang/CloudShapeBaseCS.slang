#include "Common/Binding.slangh"
#include "Common/Math.slangh"
#include "Common/Noise.slangh"

struct CloudShapeBasePush
{
    uint ID_OutBaseNoise;
};

[[vk::push_constant]] ConstantBuffer< CloudShapeBasePush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

float rbf(float d)
{
    if (d >= 1.0)
        return 0.0;

    const float t = 1.0 - d;
    return t * t * (3.0 - 2.0 * t);
}

float hashFloat(uint3 p, uint seed)
{
    return float(hash3D(p, seed)) * UIF;
}

// Difference of the two largest radial bumps among the 27 neighbouring cells; tiles with period 'frequency'
float alligatorNoise3D(float3 p, float frequency, uint seed)
{
    const float3 ip = floor(p);
    const float3 fp = frac(p);

    float max1 = 0.0;
    float max2 = 0.0;
    for (int k = -1; k <= 1; ++k)
    {
        for (int j = -1; j <= 1; ++j)
        {
            for (int i = -1; i <= 1; ++i)
            {
                const float3 g      = float3(float(i), float(j), float(k));
                const uint3  cellID = uint3(int3(modulo(ip + g, frequency)));

                float3 jitter;
                jitter.x = hashFloat(cellID, seed);
                jitter.y = hashFloat(cellID, seed + 101u);
                jitter.z = hashFloat(cellID, seed + 202u);

                const float d = length(fp - (g + jitter));
                if (d < 1.0)
                {
                    const float cellIntensity = hashFloat(cellID, seed + 303u) * 0.5 + 0.5;
                    const float val           = cellIntensity * rbf(d);
                    if (val > max1)
                    {
                        max2 = max1;
                        max1 = val;
                    }
                    else if (val > max2)
                    {
                        max2 = val;
                    }
                }
            }
        }
    }

    return max1 - max2;
}

// Not normalized: the octave sum reaches about 1.9
float alligatorFBM(float3 p, float frequency, int octaves, float persistence, float lacunarity, uint seed)
{
    float total     = 0.0;
    float amplitude = 1.0;
    for (int i = 0; i < octaves; ++i)
    {
        total += alligatorNoise3D(p * frequency, frequency, seed + uint(i * 50)) * amplitude;

        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return total;
}

[numthreads(8, 8, 8)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    RWTexture3D< float4 > OutBaseNoise = GetResource(g_Push.ID_OutBaseNoise);

    uint3 imageSize;
    OutBaseNoise.GetDimensions(imageSize.x, imageSize.y, imageSize.z);
    if (any(dispatchThreadID >= imageSize))
        return;

    const float3 uvw = (float3(dispatchThreadID) + 0.5) / float3(imageSize);

    const float alligator0 = alligatorFBM(uvw, 2.0, 4, 0.5, 2.0, 0x1511u);
    const float alligator1 = alligatorFBM(uvw, 3.0, 6, 0.5, 2.0, 0x0u);

    const float inverse0 = saturate(1.0 - alligator0);
    const float inverse1 = saturate(1.0 - alligator1);

    OutBaseNoise[dispatchThreadID] = float4(
        smoothstep(0.3, 0.7, alligator0),
        smoothstep(0.3, 0.8, alligator1),
        inverse0 * inverse0,
        (inverse1 * inverse1 + 1.0) * 0.5);
}
