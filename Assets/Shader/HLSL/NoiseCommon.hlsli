#ifndef _HLSL_NOISE_HEADER
#define _HLSL_NOISE_HEADER

#include "../Common.bsh"

float2 fade(float2 t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float3 fade(float3 t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

//////////////////////////////////////////////////////
// Reference: https://www.shadertoy.com/view/3dVXDc //
#define UI0 1597334673U
#define UI1 3812015801U
#define UI2 uint2(UI0, UI1)
#define UI3 uint3(UI0, UI1, 2798796415U)
#define UIF (1.0 / (float)0xffffffffU)

float hash1D(float3 p)
{
    p = frac(p * 1031.1031);
    p += dot(p, p.yzx + 19.19);
    return frac((p.x + p.y) * p.z);
}

float2 hash2D(float2 p)
{
    uint2 q = (uint2) ((int2) p) * UI2;
    q = (q.x ^ q.y) * UI2;

    return -1.0 + 2.0 * (float2) q * UIF;
}

float3 hash3D(float3 p)
{
    uint3 q = (uint3) ((int3) p) * UI3;
    q = (q.x ^ q.y ^ q.z) * UI3;

    return -1.0 + 2.0 * (float3) q * UIF;
}
//////////////////////////////////////////////////////

float perlinNoise2D(float2 p, float frequency)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = fade(f);

    float dot00 = dot(hash2D(fmod(i + float2(0.0, 0.0), frequency)), f - float2(0.0, 0.0));
    float dot10 = dot(hash2D(fmod(i + float2(1.0, 0.0), frequency)), f - float2(1.0, 0.0));
    float dot01 = dot(hash2D(fmod(i + float2(0.0, 1.0), frequency)), f - float2(0.0, 1.0));
    float dot11 = dot(hash2D(fmod(i + float2(1.0, 1.0), frequency)), f - float2(1.0, 1.0));

    return lerp(lerp(dot00, dot10, u.x),
                lerp(dot01, dot11, u.x), u.y);
}

float perlinNoise3D(float3 p, float frequency)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = fade(f);

    float dot000 = dot(hash3D(fmod(i + float3(0, 0, 0), frequency)), f - float3(0, 0, 0));
    float dot001 = dot(hash3D(fmod(i + float3(0, 0, 1), frequency)), f - float3(0, 0, 1));
    float dot010 = dot(hash3D(fmod(i + float3(0, 1, 0), frequency)), f - float3(0, 1, 0));
    float dot100 = dot(hash3D(fmod(i + float3(1, 0, 0), frequency)), f - float3(1, 0, 0));
    float dot011 = dot(hash3D(fmod(i + float3(0, 1, 1), frequency)), f - float3(0, 1, 1));
    float dot101 = dot(hash3D(fmod(i + float3(1, 0, 1), frequency)), f - float3(1, 0, 1));
    float dot110 = dot(hash3D(fmod(i + float3(1, 1, 0), frequency)), f - float3(1, 1, 0));
    float dot111 = dot(hash3D(fmod(i + float3(1, 1, 1), frequency)), f - float3(1, 1, 1));

    // trilinear interpolation of 8 corner contributions
    return lerp(lerp(lerp(dot000, dot100, u.x),
                     lerp(dot010, dot110, u.x), u.y),
                lerp(lerp(dot001, dot101, u.x),
                     lerp(dot011, dot111, u.x), u.y), u.z);
}

float perlinFBM(float2 p, float frequency, int octaves, float persistence, float lacunarity)
{
    float total = 0.0;
    float amplitude = 1.0;
    float normalization = 0.0;

    for (int i = 0; i < octaves; i++)
    {
        float noise = perlinNoise2D(p * frequency, frequency);

        total += noise * amplitude;
        normalization += amplitude;

        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / normalization;
}

float perlinFBM(float3 p, float frequency, uint octaves, float persistence, float lacunarity)
{
    float total = 0.0;
    float amplitude = 1.0;
    float normalization = 0.0;

    for (uint i = 0u; i < octaves; i++)
    {
        float noise = perlinNoise3D(p * frequency, frequency);

        total += noise * amplitude;
        normalization += amplitude;

        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / normalization;
}

float worleyNoise2D(float2 p, float frequency)
{
    float2 i = floor(p);
    float2 f = frac(p);

    float closest = 10000.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            {
                float2 neighbor = i + float2(x, y);
                float2 offset = hash2D(fmod(neighbor, float2(frequency, frequency))) * 0.5 + 0.5;

                float2 d = f - (float2(x, y) + offset);
                float dist = dot(d, d);
                closest = min(closest, dist);
            }
        }
    }

    return 1.0 - closest;
}

float worleyNoise3D(float3 p, float frequency)
{
    float3 i = floor(p);
    float3 f = frac(p);

    float closest = 10000.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int z = -1; z <= 1; ++z)
            {
                float3 neighbor = i + float3(x, y, z);
                float3 offset = hash3D(fmod(neighbor, float3(frequency, frequency, frequency))) * 0.5 + 0.5;

                float3 d = f - (float3(x, y, z) + offset);
                float dist = dot(d, d);
                closest = min(closest, dist);
            }
        }
    }

    return 1.0 - closest;
}

// Tileable Worley fbm inspired by Andrew Schneider's Real-Time Volumetric Cloudscapes
// chapter in GPU Pro 7.
float worleyFBM(float2 p, float frequency)
{
    return worleyNoise2D(p * frequency, frequency) * 0.625 +
           worleyNoise2D(p * frequency * 2.0, frequency * 2.0) * 0.25 +
           worleyNoise2D(p * frequency * 4.0, frequency * 4.0) * 0.125;
}

float worleyFBM(float2 p, float frequency, uint octaves, float persistence, float lacunarity)
{
    float total = 0.0;
    float amplitude = 1.0;
    float normalization = 0.0;

    for (uint i = 0u; i < octaves; i++)
    {
        float noise = worleyNoise2D(p * frequency, frequency);

        total += noise * amplitude;
        normalization += amplitude;

        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / normalization;
}

float worleyFBM(float3 p, float frequency)
{
    return worleyNoise3D(p * frequency, frequency) * 0.625 +
           worleyNoise3D(p * frequency * 2.0, frequency * 2.0) * 0.25 +
           worleyNoise3D(p * frequency * 4.0, frequency * 4.0) * 0.125;
}

float worleyFBM(float3 p, float frequency, uint octaves, float persistence, float lacunarity)
{
    float total = 0.0;
    float amplitude = 1.0;
    float normalization = 0.0;

    for (uint i = 0u; i < octaves; i++)
    {
        float noise = worleyNoise3D(p * frequency, frequency);

        total += noise * amplitude;
        normalization += amplitude;

        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / normalization;
}

float ridgedFBM(float3 p, int octaves, float persistence, float lacunarity)
{
    float total = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;
    float normalization = 0.0;

    for (int i = 0; i < octaves; i++)
    {
        float noise = perlinNoise3D(p * frequency, frequency);
        noise = abs(noise);
        noise = 1.0 - noise;

        total += noise * amplitude;
        normalization += amplitude;

        amplitude *= persistence;
        frequency *= lacunarity;
    }

    total /= normalization;
    total *= total;

    return total;
}

float turbulenceFBM(float3 p, int octaves, float persistence, float lacunarity)
{
    float total = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;
    float normalization = 0.0;

    for (int i = 0; i < octaves; i++)
    {
        float noise = perlinNoise3D(p * frequency, frequency);
        noise = abs(noise);

        total += noise * amplitude;
        normalization += amplitude;

        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / normalization;
}

float2 worleyBidirection(float3 p, float cellCount)
{
    float3 i = floor(p * cellCount);
    float3 f = frac(p * cellCount);

    float f1 = 1.0;
    float f2 = 1.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int z = -1; z <= 1; ++z)
            {
                float3 neighbor = i + float3(x, y, z);
                float3 offset = hash3D(fmod(neighbor, cellCount));

                float dist = length(float3(x, y, z) + offset - f);
                if (dist < f1)
                {
                    f2 = f1;
                    f1 = dist;
                }
                else if (dist < f2)
                {
                    f2 = dist;
                }
            }
        }
    }

    return float2(f1, f2);
}

float steppedNoise(float noiseSample)
{
    float steppedSample = floor(noiseSample * 10.0) / 10.0;
    float remainder = frac(noiseSample * 10.0);

    return (steppedSample - remainder) * 0.5 + 0.5;
}


///////////////////////////////////////////////////////
// Reference : https://www.shadertoy.com/view/NlSGDz //
// implementation of MurmurHash (https://sites.google.com/site/murmurhash/) for a single unsigned integer.
uint hash1D(uint p, uint seed)
{
    const uint m = 0x5bd1e995U;
    uint hash = seed;
    // process input
    uint k = p;
    k *= m;
    k ^= k >> 24;
    k *= m;
    hash *= m;
    hash ^= k;
    // some final mixing
    hash ^= hash >> 13;
    hash *= m;
    hash ^= hash >> 15;
    return hash;
}

// implementation of MurmurHash (https://sites.google.com/site/murmurhash/) for a 2-dimensional unsigned integer input vector.
uint hash2D(uint2 p, uint seed)
{
    const uint m = 0x5bd1e995U;
    uint hash = seed;
    // process first vector element
    uint k = p.x;
    k *= m;
    k ^= k >> 24;
    k *= m;
    hash *= m;
    hash ^= k;
    // process second vector element
    k = p.y;
    k *= m;
    k ^= k >> 24;
    k *= m;
    hash *= m;
    hash ^= k;
    // some final mixing
    hash ^= hash >> 13;
    hash *= m;
    hash ^= hash >> 15;
    return hash;
}

// implementation of MurmurHash (https://sites.google.com/site/murmurhash/) for a 3-dimensional unsigned integer input vector.
uint hash3D(uint3 p, uint seed)
{
    const uint m = 0x5bd1e995U;
    uint hash = seed;
    // process first vector element
    uint k = p.x;
    k *= m;
    k ^= k >> 24;
    k *= m;
    hash *= m;
    hash ^= k;
    // process second vector element
    k = p.y;
    k *= m;
    k ^= k >> 24;
    k *= m;
    hash *= m;
    hash ^= k;
    // process third vector element
    k = p.z;
    k *= m;
    k ^= k >> 24;
    k *= m;
    hash *= m;
    hash ^= k;
    // some final mixing
    hash ^= hash >> 13;
    hash *= m;
    hash ^= hash >> 15;
    return hash;
}

float2 gradientDirection2D(uint hash)
{
    switch ((int) hash & 3) // look at the last two bits to pick a gradient direction
    {
        case 0:
            return float2(1.0, 1.0);
        case 1:
            return float2(-1.0, 1.0);
        case 2:
            return float2(1.0, -1.0);
        case 3:
            return float2(-1.0, -1.0);
    }
    return float2(0.0, 0.0); // Should not happen
}


float3 gradientDirection3D(uint hash)
{
    switch ((int) hash & 15) // look at the last four bits to pick a gradient direction
    {
        case 0:
            return float3(1, 1, 0);
        case 1:
            return float3(-1, 1, 0);
        case 2:
            return float3(1, -1, 0);
        case 3:
            return float3(-1, -1, 0);
        case 4:
            return float3(1, 0, 1);
        case 5:
            return float3(-1, 0, 1);
        case 6:
            return float3(1, 0, -1);
        case 7:
            return float3(-1, 0, -1);
        case 8:
            return float3(0, 1, 1);
        case 9:
            return float3(0, -1, 1);
        case 10:
            return float3(0, 1, -1);
        case 11:
            return float3(0, -1, -1);
        case 12:
            return float3(1, 1, 0);
        case 13:
            return float3(-1, 1, 0);
        case 14:
            return float3(0, -1, 1);
        case 15:
            return float3(0, -1, -1);
    }
    return float3(0.0, 0.0, 0.0); // Should not happen
}
///////////////////////////////////////////////////////

float perlinNoise2D(float2 p, uint seed)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = fade(f);

    float dot00 = dot(gradientDirection2D(hash2D((uint2) i + uint2(0, 0), seed)), f - float2(0.0, 0.0));
    float dot10 = dot(gradientDirection2D(hash2D((uint2) i + uint2(1, 0), seed)), f - float2(1.0, 0.0));
    float dot01 = dot(gradientDirection2D(hash2D((uint2) i + uint2(0, 1), seed)), f - float2(0.0, 1.0));
    float dot11 = dot(gradientDirection2D(hash2D((uint2) i + uint2(1, 1), seed)), f - float2(1.0, 1.0));

    return lerp(lerp(dot00, dot10, u.x),
                lerp(dot01, dot11, u.x), u.y);
}

float perlinNoise3D(float3 p, uint seed)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = fade(f);

    float dot000 = dot(gradientDirection3D(hash3D((uint3) i + uint3(0, 0, 0), seed)), f - float3(0, 0, 0));
    float dot001 = dot(gradientDirection3D(hash3D((uint3) i + uint3(0, 0, 1), seed)), f - float3(0, 0, 1));
    float dot010 = dot(gradientDirection3D(hash3D((uint3) i + uint3(0, 1, 0), seed)), f - float3(0, 1, 0));
    float dot100 = dot(gradientDirection3D(hash3D((uint3) i + uint3(1, 0, 0), seed)), f - float3(1, 0, 0));
    float dot011 = dot(gradientDirection3D(hash3D((uint3) i + uint3(0, 1, 1), seed)), f - float3(0, 1, 1));
    float dot101 = dot(gradientDirection3D(hash3D((uint3) i + uint3(1, 0, 1), seed)), f - float3(1, 0, 1));
    float dot110 = dot(gradientDirection3D(hash3D((uint3) i + uint3(1, 1, 0), seed)), f - float3(1, 1, 0));
    float dot111 = dot(gradientDirection3D(hash3D((uint3) i + uint3(1, 1, 1), seed)), f - float3(1, 1, 1));

    // trilinear interpolation of 8 corner contributions
    return lerp(lerp(lerp(dot000, dot100, u.x),
                     lerp(dot010, dot110, u.x), u.y),
                lerp(lerp(dot001, dot101, u.x),
                     lerp(dot011, dot111, u.x), u.y), u.z);
}

float perlinFBM(float2 p, float frequency, int octaves, float persistence, float lacunarity, uint seed)
{
    float total = 0.0;
    float amplitude = 1.0;

    uint curSeed = seed;
    for (int i = 0; i < octaves; i++)
    {
        float noise = perlinNoise2D(p * frequency, curSeed);

        total += noise * amplitude;
        curSeed = hash1D(curSeed, 0x0u); // create a new seed for each octave

        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total;
}

float perlinFBM(float3 p, float frequency, uint octaves, float persistence, float lacunarity, uint seed)
{
    float total = 0.0;
    float amplitude = 1.0;

    uint curSeed = seed;
    for (uint i = 0u; i < octaves; i++)
    {
        float noise = perlinNoise3D(p * frequency, curSeed);

        total += noise * amplitude;
        curSeed = hash1D(curSeed, 0x0u); // create a new seed for each octave

        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total;
}

#endif // _HLSL_NOISE_HEADER