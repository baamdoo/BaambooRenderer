#ifndef SOBOL_SAMPLING_HLSLI
#define SOBOL_SAMPLING_HLSLI

#include "SobolTables.hlsli"

uint SobolHash2(uint a, uint b)
{
    uint state = a * 747796405u + b * 2891336453u + 277803737u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Laine-Karras style hash: every bit is perturbed only by lower bits (carries move up),
// so reverse-hash-reverse below flips each Sobol digit based only on HIGHER digits.
// Reference: Burley 2020, "Practical Hash-based Owen Scrambling".
uint LaineKarrasPermutation(uint x, uint seed)
{
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}

uint OwenScrambleBits(uint x, uint seed)
{
    x = reversebits(x);
    x = LaineKarrasPermutation(x, seed);
    return reversebits(x);
}

uint SobolSampleBits(uint sampleIndex, uint dim)
{
    uint x = 0u;
    uint k = 0u;
    while (sampleIndex != 0)
    {
        if (sampleIndex & 1)
            x ^= SOBOL_MATRICES[dim * 32 + k];

        k++;
        sampleIndex >>= 1;
    }
       
    return x;
}

// Owen-scrambled Sobol sample for a single key (pixel, sampleIndex, dim)
uint ScrambledSobolBits(uint sampleIndex, uint dim, uint pixelSeed)
{
    uint shuffledIndex = OwenScrambleBits(sampleIndex, SobolHash2(pixelSeed, 0xa511e9b3u));
    uint raw = SobolSampleBits(shuffledIndex, dim);
    return OwenScrambleBits(raw, SobolHash2(pixelSeed, 1u + dim));
}

#endif // SOBOL_SAMPLING_HLSLI
