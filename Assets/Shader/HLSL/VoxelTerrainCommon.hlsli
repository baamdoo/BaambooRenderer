#ifndef _HLSL_VOXEL_TERRAIN_COMMON_HEADER
#define _HLSL_VOXEL_TERRAIN_COMMON_HEADER

#include "NoiseCommon.hlsli"
#include "HelperFunctions.hlsli"

#define VOXEL_WORLD_FLOOR_Y_METER 0.0

// ---- Gen params + base noise ---------------------------------------------------

// SDF generation parameters (solid < 0, air > 0).
struct VoxelTerrainGenParams
{
    int   chunkCoordX, chunkCoordY, chunkCoordZ;
    float voxelSizeMeter;

    uint  cellsPerAxis;
    uint  samplesPerAxis;
    uint  apron;
    uint  seed;

    float frequency;
    uint  octaves;
    float lacunarity;
    float gain;

    float warpStrength;
    float warpFrequency;
    float mountainAmplitude;
    float detailWeight;        // slope damping (0 = plain fBm)

    float redistributionExp;   // pow(value, exp) height reshape (1 = off)
    float ridgedBlend;         // 0 = smooth fBm, 1 = ridged
    float surfaceBaseYMeter;   // base surface height (m), world-absolute
    float erosionScale;

    float erosionStrength;
    float erosionGullyWeight;
    float erosionDetail;
    float erosionOnsetInput;

    float erosionOnsetOctave;
    float erosionCellScale;
    float erosionNormalization;
    float erosionSlopeScale;

    uint  erosionOctaves;        // 0 = erosion off
    float geoMinWavelengthMeter; // density band floor (m), LOD-independent
    uint  padding1;
    uint  padding2;
};

float3 VoxelChunkOriginWS(VoxelTerrainGenParams gp)
{
    return float3(int3(gp.chunkCoordX, gp.chunkCoordY, gp.chunkCoordZ) * int(gp.cellsPerAxis)) * gp.voxelSizeMeter;
}

float3 VoxelTexelToWorld(VoxelTerrainGenParams gp, uint3 texel)
{
    int3 originIndex = int3(gp.chunkCoordX, gp.chunkCoordY, gp.chunkCoordZ) * int(gp.cellsPerAxis);
    int3 localIndex  = int3(texel) - int(gp.apron);
    return float3(originIndex + localIndex) * gp.voxelSizeMeter;
}

float2 VoxelRotScale(float2 p, float s)
{
    return float2(0.8 * p.x - 0.6 * p.y, 0.6 * p.x + 0.8 * p.y) * s;
}

// World XZ -> erosion array UV: interior texels span the chunk, apron texels overlap the neighbors
float3 VoxelErosionUV(VoxelChunkDesc chunk, float2 xzWS, float mapDim)
{
    float inner = mapDim - 2.0 * float(VOXEL_EROSION_APRON);
    float2 uv01 = (xzWS - float2(chunk.originX, chunk.originZ)) / max(chunk.chunkSizeMeter, 1e-3);
    float2 uv = (uv01 * inner + float(VOXEL_EROSION_APRON)) / mapDim;
    return float3(uv, (float) chunk.erosionSlice);
}

// Plain value-only fBm
float VoxelFbm2(VoxelTerrainGenParams gp, float2 p)
{
    float a = 0.0, amp = 1.0, sum = 0.0;
    for (uint i = 0u; i < gp.octaves; ++i)
    {
        a   += amp * valueNoiseDeriv2D(p).x;
        sum += amp;
        amp *= gp.gain;
        p    = VoxelRotScale(p, gp.lacunarity);
    }
    return a / max(sum, 1e-5);
}

// Reference: https://iquilezles.org/articles/morenoise/
float3 VoxelTerrainFBMDeriv(VoxelTerrainGenParams gp, float2 p, bool ridged)
{
    float a = 0.0, amp = 1.0, sum = 0.0;

    float2 d    = float2(0.0, 0.0); // damping accumulator
    float2 dOut = float2(0.0, 0.0); // output derivative accumulator
    for (uint i = 0u; i < gp.octaves; ++i)
    {
        float3 n   = valueNoiseDeriv2D(p);
        float  val = n.x;
        float2 der = n.yz;

        if (ridged)
        {
            der = -sign(val) * der;
            val = 1.0 - abs(val);
        }
        else
        {
            val = 0.5 * val + 0.5;
            der = 0.5 * der;
        }
        d += der;

        float w = 1.0 / (1.0 + gp.detailWeight * dot(d, d));
        a    += amp * val * w;
        dOut += amp * der * w;
        sum  += amp;
        amp  *= gp.gain;

        p = VoxelRotScale(p, gp.lacunarity);
    }

    float inv = 1.0 / max(sum, 1e-5);
    return float3(saturate(a * inv), dOut * inv);
}

// Base height + slope at a world XZ column: (h01, dh01/dx_m, dh01/dz_m)
float3 VoxelTerrainHeight01Deriv(VoxelTerrainGenParams gp, float2 xz)
{
    float2 seedOffset = float2(gp.seed * 0.1234, gp.seed * 0.5678);

    // domain warp: displace the sample coords by a low-freq fBm
    float2 wp   = xz * gp.warpFrequency + seedOffset;
    float2 warp = float2(VoxelFbm2(gp, wp), VoxelFbm2(gp, wp + float2(31.4, 17.7)));

    float2 p = xz * gp.frequency + seedOffset + gp.warpStrength * warp;

    float3 sm = VoxelTerrainFBMDeriv(gp, p, false);
    float3 rg = VoxelTerrainFBMDeriv(gp, p, true);

    float  t  = saturate(gp.ridgedBlend);
    float  v  = lerp(sm.x, rg.x, t);
    float2 dv = lerp(sm.yz, rg.yz, t);

    // redistribution chain rule: d(v^e) = e * v^(e-1) * dv
    float  e  = max(gp.redistributionExp, 1e-3);
    float  h  = pow(saturate(v), e);
    float2 dh = e * pow(max(v, 1e-4), e - 1.0) * dv;

    // noise space -> world meters (p = xz * frequency)
    return float3(h, dh * gp.frequency);
}

float VoxelTerrainHeight01(VoxelTerrainGenParams gp, float2 xz)
{
    return VoxelTerrainHeight01Deriv(gp, xz).x;
}

float2 VoxelTerrainCoarseGrad(VoxelTerrainGenParams gp, float2 xz, float spacing)
{
    float h  = max(spacing, 1e-3);
    float dx = VoxelTerrainHeight01(gp, xz + float2(h, 0.0)) - VoxelTerrainHeight01(gp, xz - float2(h, 0.0));
    float dz = VoxelTerrainHeight01(gp, xz + float2(0.0, h)) - VoxelTerrainHeight01(gp, xz - float2(0.0, h));
    return float2(dx, dz) / (2.0 * h);
}

// ---- Detailed Erosion --------------------------------------------------------------
// Reference: https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html

float2 VoxelErosionHash2(float2 x, uint seed)
{
    uint h = hash2D(uint2(int2(x)), seed);
    return -1.0 + 2.0 * float2(h & 0xFFFFu, h >> 16u) / 65535.0;
}
float VoxelEaseOut(float t) { float v = 1.0 - saturate(t); return 1.0 - v * v; }
float VoxelPowInv(float t, float power) { return 1.0 - pow(1.0 - saturate(t), power); }
float VoxelSmoothStart(float t, float smoothing)
{
    if (t >= smoothing)
        return t - 0.5 * smoothing;

    return 0.5 * t * t / max(smoothing, 1e-6);
}

float4 PhacelleNoise(float2 p, float2 gradientDir, float freq, float offset, float normalization, uint seed)
{
    float2 sideDir = float2(-gradientDir.y, gradientDir.x) * freq * 2.0 * PI;

    // 4x4 grid neighborhood around p
    float2 base   = floor(p);
    float2 remain = frac(p);

    float weightSum = 0.0;
    float2 phaseDir = float2(0.0, 0.0);
    for (int x = -1; x <= 2; ++x)
    {
        for (int z = -1; z <= 2; ++z)
        {
            float2 gridOffset = float2(x, z);
            float2 gridPoint  = base + gridOffset;

            float2 randomOffset       = VoxelErosionHash2(gridPoint, seed) * 0.5;
            float2 vectorFromSplatToP = remain - gridOffset - randomOffset;
            float  distSq = dot(vectorFromSplatToP, vectorFromSplatToP);

            // Gaussian-shaped weight: exp(-2*d^2) - 0.01111 (exactly 0 at d=1.5)
            float weight = max(0.0, exp(-2.0 * distSq) - 0.01111);
            weightSum += weight;

            float waveInput = dot(vectorFromSplatToP, sideDir) + offset * 2.0 * PI;
            phaseDir += float2(cos(waveInput), sin(waveInput)) * weight;
        }
    }

    float2 interpolated = phaseDir / weightSum;
    float  magnitude    = max(1.0 - normalization, length(interpolated));

    return float4(interpolated / magnitude, sideDir);
}

// Erosion constants.
static const float kEroRoundingRidge     = 0.1;  // ridge rounding (lerp hi endpoint)
static const float kEroRoundingCrease    = 0.0;  // crease rounding (lerp lo endpoint; 0 = sharp creases)
static const float kEroRoundingInput     = 0.1;  // input-feature rounding scale
static const float kEroRoundingOctave    = 2.0;  // per-octave rounding multiplier
static const float kEroOnsetRidgeIn      = 2.8;  // ridge-map input mask onset
static const float kEroOnsetRidgeOct     = 1.5;  // ridge-map octave mask onset
static const float kEroAssumedSlopeMag   = 0.7;  // pretend input slope magnitude (straight-gullies aid)
static const float kEroAssumedSlopeBlend = 1.0; // 0 = real slope, 1 = fully assumed
static const float kEroGain              = 0.5;  // per-octave strength decay
static const float kEroLacunarity        = 2.0;  // per-octave frequency step

float4 VoxelErosionFilterEx(VoxelTerrainGenParams gp, float2 p, float3 heightAndSlope, float fadeTarget,
                            float geoMinWavelengthMeter, float outMinWavelengthMeter,
                            out float ridgeMap, out float3 geoDelta)
{
    float strength    = gp.erosionStrength * gp.erosionScale;
    float freq        = 1.0 / max(gp.erosionScale * gp.erosionCellScale, 1e-4);
    float slopeLength = max(length(heightAndSlope.yz), 1e-6);

    fadeTarget = clamp(fadeTarget, -1.0, 1.0);

    float3 inputHeightAndSlope = heightAndSlope;
    float  magnitude    = 0.0;
    float  roundingMult = 1.0;

    float roundingForInput = lerp(kEroRoundingCrease, kEroRoundingRidge, saturate(fadeTarget + 0.5)) * kEroRoundingInput;
    // accumulating slope mask (input slope first, then each octave)
    float combiMask = VoxelEaseOut(VoxelSmoothStart(slopeLength * gp.erosionOnsetInput, roundingForInput * gp.erosionOnsetInput));

    // Ridge map: parallel copies of fadeTarget and mask.
    float ridgeMapCombiMask  = VoxelEaseOut(slopeLength * kEroOnsetRidgeIn);
    float ridgeMapFadeTarget = fadeTarget;

    // Initial gully-direction slope: mix of the actual slope and an assumed-magnitude slope.
    float2 gullySlope = lerp(heightAndSlope.yz, heightAndSlope.yz / slopeLength * kEroAssumedSlopeMag, kEroAssumedSlopeBlend);

    bool geoRecorded = false;
    geoDelta = float3(0.0, 0.0, 0.0);

    for (uint i = 0u; i < gp.erosionOctaves; ++i)
    {
        // band limit: octaves finer than the consumer grid only alias
        float wavelength = 1.0 / (freq * gp.erosionCellScale);
        if (!geoRecorded && wavelength < geoMinWavelengthMeter)
        {
            geoDelta    = heightAndSlope - inputHeightAndSlope; // geometry-band snapshot
            geoRecorded = true;
        }
        if (wavelength < outMinWavelengthMeter)
            break;

        // normalized gullySlope; zero slope falls back to +x
        float  gl      = length(gullySlope);
        float2 normDir = (gl > 1e-8) ? gullySlope / gl : float2(1.0, 0.0);

        float4 phacelle = PhacelleNoise(p * freq, normDir, gp.erosionCellScale, 0.25, gp.erosionNormalization, gp.seed);

        // Multiply with freq since p was multiplied with freq. Negate: slope directions point down.
        phacelle.zw *= -freq;

        // Amount of slope as a 0..1 value.
        float sloping = abs(phacelle.y);

        // rule 1 - direction inheritance: sign(sin) straight-gullies trick feeds later octaves
        gullySlope += sign(phacelle.y) * phacelle.zw * strength * gp.erosionGullyWeight;

        // Gullies: height offset (-1..1) in x, derivative in yz.
        float3 gullies = float3(phacelle.x, phacelle.y * phacelle.zw);

        // rule 2 - fade gullies towards fadeTarget by combiMask (flats finish at ridge/valley value)
        float3 fadedGullies = lerp(float3(fadeTarget, 0.0, 0.0), gullies * gp.erosionGullyWeight, combiMask);

        heightAndSlope += fadedGullies * strength;
        magnitude      += strength;

        // Fade stacking: this octave's faded output is the next octave's fade destination.
        fadeTarget = fadedGullies.x;

        // rule 3 - sanctuary mask: ridges/creases carved so far are not re-carved
        float roundingForOctave = lerp(kEroRoundingCrease, kEroRoundingRidge, saturate(phacelle.x + 0.5)) * roundingMult;
        float newMask = VoxelEaseOut(VoxelSmoothStart(sloping * gp.erosionOnsetOctave, roundingForOctave * gp.erosionOnsetOctave));
        combiMask = VoxelPowInv(combiMask, gp.erosionDetail) * newMask;

        // Ridge-map parallel track.
        ridgeMapFadeTarget = lerp(ridgeMapFadeTarget, gullies.x, ridgeMapCombiMask);
        ridgeMapCombiMask *= VoxelEaseOut(sloping * kEroOnsetRidgeOct);

        strength     *= kEroGain;
        freq         *= kEroLacunarity;
        roundingMult *= kEroRoundingOctave;
    }

    ridgeMap = ridgeMapFadeTarget * (1.0 - ridgeMapCombiMask);

    if (!geoRecorded)
        geoDelta = heightAndSlope - inputHeightAndSlope; // geo limit not reached

    return float4(heightAndSlope - inputHeightAndSlope, magnitude);
}

// Geometry-band wrapper: evaluates down to the geometry wavelength only
float4 VoxelErosionFilter(VoxelTerrainGenParams gp, float2 p, float3 heightAndSlope, float fadeTarget, out float ridgeMap)
{
    float3 geoUnused;
    return VoxelErosionFilterEx(gp, p, heightAndSlope, fadeTarget, gp.geoMinWavelengthMeter, gp.geoMinWavelengthMeter, ridgeMap, geoUnused);
}

// Base height + erosion delta
float VoxelTerrainErodedHeight01(VoxelTerrainGenParams gp, float2 xz)
{
    float3 heightSlope = VoxelTerrainHeight01Deriv(gp, xz);
    if (gp.erosionOctaves == 0u)
        return heightSlope.x;

    // fade target: valley -1 .. peak +1
    float fadeTarget = clamp((heightSlope.x - 0.5) * 2.0 / 0.6, -1.0, 1.0);

    // filter slope: coarse FD of the final height
    float  amp = max(gp.mountainAmplitude, 1e-4);
    float2 g   = VoxelTerrainCoarseGrad(gp, xz, 0.25 * gp.erosionScale * gp.erosionCellScale);
    float3 hs  = float3(heightSlope.x * amp, g * amp * gp.erosionSlopeScale);

    float  ridge;
    float4 d = VoxelErosionFilter(gp, xz, hs, fadeTarget, ridge);

    return saturate(heightSlope.x + d.x / amp); // delta meters -> h01
}

// Density at a world position (SDF: solid < 0, air > 0)
float VoxelTerrainDensity(VoxelTerrainGenParams gp, float3 worldPos)
{
    float h01        = VoxelTerrainErodedHeight01(gp, worldPos.xz);
    float surfaceY   = gp.surfaceBaseYMeter + (h01 - 0.5) * gp.mountainAmplitude;
    surfaceY         = max(surfaceY, VOXEL_WORLD_FLOOR_Y_METER + 0.25); // keeps the y=0 sample solid at every LOD
    float surfaceSDF = worldPos.y - surfaceY; // solid (<0) below the surface
    float floorSDF   = (VOXEL_WORLD_FLOOR_Y_METER - 64.0) - worldPos.y; // one chunk below the visible world

    return max(surfaceSDF, floorSDF); // intersection: below surface AND above the terrain base floor
}

// ---- Dicing helpers --------------------------------------------------------------

// Base tris per MS group by budget level Lm (1..3)
static const uint kDiceTrisPerGroup[4] = { 21u, 10u, 4u, 1u };

uint DiceSubTriCount(uint level)  { return 1u << (2u * level); }
uint DiceSubVertCount(uint level) { uint n = 1u << level; return (n + 1u) * (n + 2u) / 2u; }

float DiceLodLevel(float d, float e, VoxelChunkDesc desc)
{
    return log2(desc.diceKScale * e / (desc.diceTargetPx * d));
}

uint DiceEdgeLevel(float3 p0, float3 p1, float3 camPos, VoxelChunkDesc desc)
{
    precise float dMid = distance(0.5 * (p0 + p1), camPos);

    float Le = (dMid >= desc.diceRadiusMeter) ? // lod-0 if dice radius exceeded (outside fade)
		0.0 : clamp(ceil(DiceLodLevel(dMid, length(p1 - p0), desc)), 1.0, (float)desc.diceMaxLevel);
    return (uint)Le;
}

uint DiceSnapEdgeK(uint k, uint Lt, uint Le, bool bLower)
{
    uint s = 1u << (Lt - Le); // snap stride
	if (k % s == s / 2)
	{
        if (bLower)
            k -= s / 2; // tie-break toward the lower sub-vertex
        else
            k += s / 2; // tie-break toward the higher sub-vertex
    }

    return round((float)k / (float)s) * s;  // round to nearest multiple of s (s = 2^m -> exact FP)
}

// Conservative meshlet budget level: an upper bound on every edge level inside.
uint DiceMeshletBudgetLevel(float3 centerWS, float radiusWS, float3 camWS, VoxelChunkDesc desc)
{
    float dMin = max(distance(centerWS, camWS) - radiusWS, 1e-3);
    if (dMin >= desc.diceRadiusMeter)
        return 0u;

    float eMax = 1.7320508 * desc.voxelSizeMeter; // sqrt(3) * voxelSize (longest edge)
    float lm   = ceil(DiceLodLevel(dMin, eMax, desc));
    return (uint)clamp(lm, 1.0, (float)min(desc.diceMaxLevel, 3u));
}

// MS group count one payload slot needs at level
uint DiceGroupsForMeshlet(uint lm, uint triCount)
{
    uint t = kDiceTrisPerGroup[lm];
    return (triCount + t - 1u) / t;
}

// Integer barycentric sub-vertex coord
uint3 DiceSubVertexCoordInt(uint sv, uint level)
{
    uint n = 1u << level;

    uint i = (uint)((sqrt(8.0 * (float)sv + 1.0) - 1.0) * 0.5);
    while ((i + 1u) * (i + 2u) / 2u <= sv)
        ++i; // float sqrt can land a row off
    while (i * (i + 1u) / 2u > sv)
        --i;

    uint j = sv - i * (i + 1u) / 2u;
    return uint3(n - i, i - j, j);
}

float3 DiceSubVertexBary(uint sv, uint level)
{
    return float3(DiceSubVertexCoordInt(sv, level)) / (float)(1u << level);
}

// Canonical sub-triangle enumeration
uint3 DiceSubTriVerts(uint st, uint level)
{
    uint r = (uint)sqrt((float)st);
    while ((r + 1u) * (r + 1u) <= st) 
        ++r;
    while (r * r > st)
        --r;

    uint m = st - r * r;
    uint k = m >> 1u;

    uint rowA = r * (r + 1u) / 2u;
    uint rowB = (r + 1u) * (r + 2u) / 2u;

    return ((m & 1u) == 0u)
        ? uint3(rowA + k, rowB + k,      rowB + k + 1u)  // upright
        : uint3(rowA + k, rowB + k + 1u, rowA + k + 1u); // inverted
}

// Lexicographic endpoint order; fixes the tie-break direction per edge.
bool DiceLexLess(float3 a, float3 b)
{
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
}

// Snap an integer sub-vertex coord onto the owning edge
uint3 DiceSnapSubVertexCoord(uint3 coord, uint Lt, uint3 le, float3 p0, float3 p1, float3 p2)
{
    uint n = 1u << Lt;

    if (coord.z == 0u && coord.x != 0u && coord.y != 0u)      // on edge p0-p1, k runs p0 -> p1
    {
        uint k = DiceSnapEdgeK(coord.y, Lt, le.x, DiceLexLess(p0, p1));
        coord  = uint3(n - k, k, 0u);
    }
    else if (coord.x == 0u && coord.y != 0u && coord.z != 0u) // on edge p1-p2, k runs p1 -> p2
    {
        uint k = DiceSnapEdgeK(coord.z, Lt, le.y, DiceLexLess(p1, p2));
        coord  = uint3(0u, n - k, k);
    }
    else if (coord.y == 0u && coord.z != 0u && coord.x != 0u) // on edge p2-p0, k runs p2 -> p0
    {
        uint k = DiceSnapEdgeK(coord.x, Lt, le.z, DiceLexLess(p2, p0));
        coord  = uint3(k, 0u, n - k);
    }
    return coord;
}

// Sub-vertex -> chunk-local position/normal: snap, then one barycentric lerp.
void DiceSubVertex(uint3 coord, uint Lt, uint3 le, float3 p0, float3 p1, float3 p2, float3 n0,  float3 n1,  float3 n2, out float3 pos, out float3 nrm)
{
    coord = DiceSnapSubVertexCoord(coord, Lt, le, p0, p1, p2);

    precise float  invN = 1.0 / (float)(1u << Lt);
    precise float3 b    = float3(coord) * invN;
    precise float3 p    = b.x * p0 + b.y * p1 + b.z * p2;
    pos = p;
    nrm = b.x * n0 + b.y * n1 + b.z * n2;
}


// ---- Micro band ------------------------------------------------------------------
struct DiceMicroParams
{
    float amplitudeMeter;
    float baseWaveLengthMeter;
    float lacunarity;
    float gain;
    float sharpness;    // -1 = ridged (rock crests) .. 0 = plain .. +1 = billowed (crevices)
    float refEdgeMeter; // band-limit nominal edge E0 (= voxelSizeMeter)
    uint  octaves;
};

DiceMicroParams DiceMicroFromDesc(VoxelChunkDesc desc)
{
    DiceMicroParams mp;
    mp.amplitudeMeter      = desc.microAmplitudeMeter;
    mp.baseWaveLengthMeter = max(desc.microBaseWaveLengthMeter, 1e-3);
    mp.lacunarity          = max(desc.microLacunarity, 1.01);
    mp.gain                = desc.microGain;
    mp.sharpness           = desc.microSharpness;
    mp.refEdgeMeter        = desc.voxelSizeMeter;
    mp.octaves             = min(desc.microOctaves, 6u);
    return mp;
}

// Micro band height + world-space slope
float3 VoxelMicroHeightDeriv(float2 xzWS, float lv, DiceMicroParams mp)
{
    // fixed per-octave rotation hides lattice alignment
    const float2x2 R = float2x2(0.8, -0.6, 0.6, 0.8);

    float invWL = 1.0 / mp.baseWaveLengthMeter;

    float2   p = xzWS * invWL;
    float2x2 M = float2x2(invWL, 0.0, 0.0, invWL); // dp/dxz chain-rule Jacobian (rotations + frequency)

    float  amp  = mp.amplitudeMeter;
    float  wl   = mp.baseWaveLengthMeter;
    float2 dsum = float2(0.0, 0.0); // damping accumulator

    float  h      = 0.0;
    float2 dWorld = float2(0.0, 0.0);
    [loop] for (uint i = 0u; i < mp.octaves; ++i)
    {
        float3 n = valueNoiseDeriv2D(p); // (value, d/dp)

        // sharpness fold
        float  s    = mp.sharpness;
        float  sgnS = (s >= 0.0) ? 1.0 : -1.0;
        float  sgnN = (n.x >= 0.0) ? 1.0 : -1.0;
        float  v    = lerp(n.x, sgnS * (2.0 * abs(n.x) - 1.0), abs(s));
        v += sgnS * abs(s) * 0.2424; // zero-center the fold
        float2 dv   = n.yz * lerp(1.0, 2.0 * sgnS * sgnN, abs(s));

        // slope damping
        dsum += dv;
        float wErode = 1.0 / (1.0 + dot(dsum, dsum));

        // band-limit fade: Nyquist needs wl >= 2 * (refEdge / 2^lv); linear fade over one level
        float lNeed = log2(2.0 * mp.refEdgeMeter / wl);
        float wFade = saturate(lv - lNeed + 1.0);
        if (wFade <= 0.0)
            break;

        float wAmp = amp * wErode * wFade;
        h      += wAmp * v;
        dWorld += wAmp * mul(transpose(M), dv);

        amp *= mp.gain;
        wl  /= mp.lacunarity;
        p   = mul(R, p) * mp.lacunarity;
        M   = mul(R, M) * mp.lacunarity;
    }

    return float3(h, dWorld);
}

float3 DisplaceVoxelDice(float3 posWS, float baseNy, float3 camPosWS, VoxelChunkDesc chunk, Texture2DArray< float4 > ErosionMap, SamplerState Sampler)
{
    if (chunk.erosionSlice == INVALID_INDEX)
        return posWS; // coarse-LOD chunks carry no erosion slice

    uint mapW, mapH, mapSlices;
    ErosionMap.GetDimensions(mapW, mapH, mapSlices);

    float4 erosion = ErosionMap.SampleLevel(Sampler, VoxelErosionUV(chunk, posWS.xz, (float)mapW), 0); // R detail height (m) | G ridgeMap | B surfaceY | A unused

    float dCam      = length(posWS - camPosWS);
    float wDist     = saturate((chunk.diceRadiusMeter - dCam) / chunk.diceFadeWidthMeter); // fade out at the dicing radius
    float hfGate    = saturate(1.0 - abs(posWS.y - erosion.b) / 2.0);                      // height-field gate, +/-2 m tolerance
    float slopeGate = saturate(baseNy);                                                    // no displacement on steep slopes (avoid self-intersection)

    // diced band
    float h = erosion.r * chunk.diceDisplacementScale;

    // micro band
    if (chunk.microOctaves != 0u)
    {
        DiceMicroParams mpar = DiceMicroFromDesc(chunk);

        float lv = min(DiceLodLevel(dCam, chunk.voxelSizeMeter, chunk), (float)chunk.diceMaxLevel);
        h += VoxelMicroHeightDeriv(posWS.xz, lv, mpar).x * (1.0 + chunk.microCreaseBoost * saturate(-erosion.g));
    }

    return posWS + float3(0.0, hfGate * slopeGate * wDist * h, 0.0);
}


// ---- Vertex ------------------------------------------------------------------------

// 8+8 bit octahedral unit vector
uint VoxelPackNormal16(float3 n)
{
    float denom = abs(n.x) + abs(n.y) + abs(n.z);
    if (denom < 1e-6)
        return 0u;

    float2 oct = n.xy / denom;
    if (n.z < 0.0)
        oct = float2((1.0 - abs(oct.y)) * (oct.x >= 0.0 ? 1.0 : -1.0),
                     (1.0 - abs(oct.x)) * (oct.y >= 0.0 ? 1.0 : -1.0));

    uint2 q = uint2(round((oct * 0.5 + 0.5) * 255.0));
    return q.x << 8u | q.y;
}

float3 VoxelUnpackNormal16(uint bits16)
{
    float2 oct = float2((bits16 >> 8u) & 0xFFu, bits16 & 0xFFu) / 255.0 * 2.0 - 1.0;

    float z = 1.0 - abs(oct.x) - abs(oct.y);
    if (z < 0.0)
        oct = float2((1.0 - abs(oct.y)) * (oct.x >= 0.0 ? 1.0 : -1.0),
                     (1.0 - abs(oct.x)) * (oct.y >= 0.0 ? 1.0 : -1.0));

    return normalize(float3(oct.x, oct.y, z));
}

// The normal exactly as the renderer decodes it (the geomorph ray must use this one)
float3 VoxelQuantizeNormal(float3 n)
{
    return VoxelUnpackNormal16(VoxelPackNormal16(n));
}

// 10-bit two's complement of x in [-1, 1]
uint VoxelPackSnorm10(float x)
{
    return uint(int(round(clamp(x, -1.0, 1.0) * 511.0))) & 0x3FFu;
}

float VoxelUnpackSnorm10(uint bits10)
{
    return float(int(bits10 << 22u) >> 22) / 511.0;
}

// s1/s2/s3 = signed offsets along the normal to the parent / grandparent / great-grandparent mesh (units 2/4/8 voxels); pinned = transition-cell coarse face, never shifted
VoxelVertex VoxelPackVertex(float3 pos, float3 normal, float3 normalT1, float3 normalT2, float s1, float s2, float s3, bool pinned, float chunkSizeMeter, float voxelSizeMeter)
{
    uint3 posQ = uint3(round(saturate(pos / chunkSizeMeter) * 65535.0));

    VoxelVertex v;
    v.posXY      = posQ.x << 16u | posQ.y;
    v.posZnormal = posQ.z << 16u | VoxelPackNormal16(normal);
    v.normalT    = VoxelPackNormal16(normalT1) << 16u | VoxelPackNormal16(normalT2);
    v.morph      = (pinned ? 1u : 0u)
                 | VoxelPackSnorm10(s1 / (2.0 * voxelSizeMeter)) << 1u
                 | VoxelPackSnorm10(s2 / (4.0 * voxelSizeMeter)) << 11u
                 | VoxelPackSnorm10(s3 / (8.0 * voxelSizeMeter)) << 21u;
    return v;
}

float3 VoxelUnpackPos(VoxelVertex v, float chunkSizeMeter)
{
    uint3 posQ = uint3(v.posXY >> 16u, v.posXY & 0xFFFFu, v.posZnormal >> 16u);
    return float3(posQ) / 65535.0 * chunkSizeMeter;
}

float3 VoxelUnpackNormal(VoxelVertex v)
{
    return VoxelUnpackNormal16(v.posZnormal & 0xFFFFu);
}

float3 VoxelUnpackNormalT1(VoxelVertex v)
{
    return VoxelUnpackNormal16(v.normalT >> 16u);
}

float3 VoxelUnpackNormalT2(VoxelVertex v)
{
    return VoxelUnpackNormal16(v.normalT & 0xFFFFu);
}

// (s1, s2, s3) in meters
float3 VoxelUnpackMorph(VoxelVertex v, float voxelSizeMeter)
{
    return float3(VoxelUnpackSnorm10((v.morph >> 1u)  & 0x3FFu) * 2.0,
                  VoxelUnpackSnorm10((v.morph >> 11u) & 0x3FFu) * 4.0,
                  VoxelUnpackSnorm10((v.morph >> 21u) & 0x3FFu) * 8.0) * voxelSizeMeter;
}

float3 VoxelTransitionDelta(float3 posQ, uint transitionMask)
{
    const float cellWidth = 65535.0 / 128.0; // pos(uint16) resolution / 128 cells per chunk

    bool isBoundaryXMin = posQ.x < cellWidth;           // vertex inside the -x boundary cell
    bool isBoundaryXMax = posQ.x > 65535.0 - cellWidth; // vertex inside the +x boundary cell
    bool isBoundaryYMin = posQ.y < cellWidth;           // vertex inside the -y boundary cell
    bool isBoundaryYMax = posQ.y > 65535.0 - cellWidth; // vertex inside the +y boundary cell
    bool isBoundaryZMin = posQ.z < cellWidth;           // vertex inside the -z boundary cell
    bool isBoundaryZMax = posQ.z > 65535.0 - cellWidth; // vertex inside the +z boundary cell

    float3 delta = float3(0.0, 0.0, 0.0);
    if      (isBoundaryXMin && (transitionMask & (1u << 0u)) != 0u) delta.x = +0.5 * (cellWidth - posQ.x);
    else if (isBoundaryXMax && (transitionMask & (1u << 1u)) != 0u) delta.x = -0.5 * (cellWidth - (65535.0 - posQ.x));
    if      (isBoundaryYMin && (transitionMask & (1u << 2u)) != 0u) delta.y = +0.5 * (cellWidth - posQ.y);
    else if (isBoundaryYMax && (transitionMask & (1u << 3u)) != 0u) delta.y = -0.5 * (cellWidth - (65535.0 - posQ.y));
    if      (isBoundaryZMin && (transitionMask & (1u << 4u)) != 0u) delta.z = +0.5 * (cellWidth - posQ.z);
    else if (isBoundaryZMax && (transitionMask & (1u << 5u)) != 0u) delta.z = -0.5 * (cellWidth - (65535.0 - posQ.z));

    return delta;
}

// Render-position unpack: boundary-band vertices shift inward on faces whose transition mask bit is set
float3 VoxelUnpackPosTransition(VoxelVertex v, float chunkSizeMeter, uint lodAndMask)
{
    float3 posF = float3(uint3(v.posXY >> 16u, v.posXY & 0xFFFFu, v.posZnormal >> 16u));
    if ((v.morph & 1u) == 0u) // pinned vertices sit on the coarse face itself
    {
        // slide along the tangent plane so the shifted vertex stays on the surface (uniform scale keeps the normal valid in quantized space)
        float3 n     = VoxelUnpackNormal(v);
        float3 delta = VoxelTransitionDelta(posF, (lodAndMask >> 8u) & 0x3Fu);
        posF += delta - dot(delta, n) * n;
    }
    return posF / 65535.0 * chunkSizeMeter;
}

// Geomorph fade alpha
float VoxelMorphFactor(float3 posWS, float3 camPosWS, float chunkSizeMeter)
{
    float d = length(posWS - camPosWS);
    return saturate((d - chunkSizeMeter) / (0.5 * chunkSizeMeter));
}

// Morph a fine vertex along its normal toward the coarse surface
float3 VoxelMorphPosWS(VoxelVertex v, float3 posWS, float3 n, float3 camPosWS, VoxelChunkDesc chunk)
{
    float  t1 = VoxelMorphFactor(posWS, camPosWS, chunk.chunkSizeMeter);
    float  t2 = VoxelMorphFactor(posWS, camPosWS, 2.0 * chunk.chunkSizeMeter);
    float  t3 = VoxelMorphFactor(posWS, camPosWS, 4.0 * chunk.chunkSizeMeter);
    float3 s  = VoxelUnpackMorph(v, chunk.voxelSizeMeter);
    return posWS + lerp(lerp(t1 * s.x, s.y, t2), s.z, t3) * n;
}

float3 VoxelMorphNormal(VoxelVertex v, float3 n, float3 posWS, float3 camPosWS, VoxelChunkDesc chunk)
{
    float  t1 = VoxelMorphFactor(posWS, camPosWS, chunk.chunkSizeMeter);
    float  t2 = VoxelMorphFactor(posWS, camPosWS, 2.0 * chunk.chunkSizeMeter);
    float3 n1 = lerp(n, VoxelUnpackNormalT1(v), t1);
    return normalize(lerp(n1, VoxelUnpackNormalT2(v), t2));
}


// ---- Marching Cubes shared ----------------------------------------------------------

// corner i -> unit-cube offset
static const uint3 kCornerOffset[8] =
{
    uint3(0, 0, 0), uint3(1, 0, 0), uint3(1, 1, 0), uint3(0, 1, 0),
    uint3(0, 0, 1), uint3(1, 0, 1), uint3(1, 1, 1), uint3(0, 1, 1)
};

// edge -> its two corners
static const uint2 kEdgeCorners[12] =
{
    uint2(0, 1), uint2(1, 2), uint2(2, 3), uint2(3, 0),
    uint2(4, 5), uint2(5, 6), uint2(6, 7), uint2(7, 4),
    uint2(0, 4), uint2(1, 5), uint2(2, 6), uint2(3, 7)
};

// Isosurface point on the edge (v0, v1): position interpolates the two corner samples, normal = density gradient.
void VoxelEdgePoint(float v0, float v1, float3 p0, float3 p1, float3 g0, float3 g1, out float3 pos, out float3 n)
{
    float  tt  = (abs(v0 - v1) < 1e-6) ? 0.0 : v0 / (v0 - v1); // zero crossing along the edge
    float3 g   = lerp(g0, g1, tt);
    float  gl2 = dot(g, g);

    pos = lerp(p0, p1, tt);
    n   = (gl2 > 1e-12) ? g * rsqrt(gl2) : float3(0.0, 1.0, 0.0);
}

// ---- Geomorphing ------------------------------------------------------------------
bool RayTriangle(float3 o, float3 d, float3 t0, float3 t1, float3 t2, out float s, out float3 bary)
{
    s    = 0.0;
    bary = float3(0.0, 0.0, 0.0);
    
    float3 e1 = t1 - t0;
    float3 e2 = t2 - t0;
    
    // Möller–Trumbore intersection
    float3 P   = cross(d, e2);
    float  det = dot(e1, P);
    if (abs(det) < 1e-6) // parallel
        return false;
    float invDet = 1.0 / det;
    
    float3 T = o - t0;
    
    float u = dot(T, P) * invDet;
    if (u < 0.0 || u > 1.0)
        return false; // out of triangle
    
    float3 Q = cross(T, e1);
    
    float v = dot(d, Q) * invDet;
    if (v < 0.0 || u + v > 1.0)
        return false; // out of triangle
    
    s    = dot(e2, Q) * invDet;
    bary = float3(1.0 - u - v, u, v);
    return true;
}

struct VoxelProjectResult
{
    float  sd;      // signed distance along the fine normal to the coarse mesh (m)
    float3 nTarget; // coarse shading normal at the hit (barycentric blend of the coarse vertex normals)
    float3 nFace;   // coarse triangle face normal at the hit
    uint   code;    // 0 hit | 1 empty cell | 2 no hit within radius | 3 back-facing only
};

int3 VoxelCoarseOrigin(VoxelTerrainGenParams gp, float3 pos, uint stride)
{
    int3 origin = int3(floor(pos / (float(stride) * gp.voxelSizeMeter))) * stride;
    int lo = int(stride) - int(gp.apron);
    int hi = int(gp.cellsPerAxis) + int(gp.apron) - 2 * int(stride);
    return clamp(origin, int3(lo, lo, lo), int3(hi, hi, hi));
}

// Coarse cell (stride^3 fine cells) at `coarseOrigin`, read as the coarser LOD's MC would see it; per-thread globals so the projection loops pass no arrays around
static float  s_CoarseDensity[8];
static float3 s_CoarsePos[8];
static float3 s_CoarseGrad[8];
void GetCoarseGeometry(VoxelTerrainGenParams gp, int3 coarseOrigin, uint stride, StructuredBuffer< float > Density)
{
    const uint dim = gp.cellsPerAxis + 1u + 2u * gp.apron;

    [unroll] for (uint i = 0u; i < 8u; ++i)
    {
        int3  gc = coarseOrigin + stride * int3(kCornerOffset[i]);
        uint3 tx = uint3(gc + int(gp.apron));

        s_CoarseDensity[i] = Density[FlatTexel(tx, dim)];
        s_CoarsePos[i]     = float3(gc) * gp.voxelSizeMeter;
        s_CoarseGrad[i]    = float3(
            Density[FlatTexel(tx + uint3(stride, 0u, 0u), dim)] - Density[FlatTexel(tx - uint3(stride, 0u, 0u), dim)],
            Density[FlatTexel(tx + uint3(0u, stride, 0u), dim)] - Density[FlatTexel(tx - uint3(0u, stride, 0u), dim)],
            Density[FlatTexel(tx + uint3(0u, 0u, stride), dim)] - Density[FlatTexel(tx - uint3(0u, 0u, stride), dim)]);
    }
}

// Tests every coarse triangle of one cell against the ray finePos + s*n; keeps the closest same-facing hit in r
void ProjectTestCell(VoxelTerrainGenParams gp, int3 coarseOrigin, float3 finePos, float3 n, float radiusMeter, uint stride,
                     StructuredBuffer< float > Density, StructuredBuffer< int > TriTable, inout VoxelProjectResult r, inout uint cellFlags)
{
    GetCoarseGeometry(gp, coarseOrigin, stride, Density);

    uint cubeIndex = 0u;
    [unroll] for (uint i = 0u; i < 8u; ++i)
    {
        if (s_CoarseDensity[i] < 0.0)
            cubeIndex |= (1u << i); // solid corner sets the bit
    }
    if (cubeIndex == 0u || cubeIndex == 255u)
        return; // fully inside/outside -> no surface
    cellFlags |= 1u;

    [loop] for (uint e = 0u; e < 15u; e += 3u)
    {
        int i0 = TriTable[cubeIndex * 16u + e + 0];
        int i1 = TriTable[cubeIndex * 16u + e + 1];
        int i2 = TriTable[cubeIndex * 16u + e + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0)
            break;

        uint2 ea = kEdgeCorners[i0], eb = kEdgeCorners[i1], ec = kEdgeCorners[i2];
        float3 pa, pb, pc;
        float3 na, nb, nc;
        VoxelEdgePoint(s_CoarseDensity[ea.x], s_CoarseDensity[ea.y], s_CoarsePos[ea.x], s_CoarsePos[ea.y], s_CoarseGrad[ea.x], s_CoarseGrad[ea.y], pa, na);
        VoxelEdgePoint(s_CoarseDensity[eb.x], s_CoarseDensity[eb.y], s_CoarsePos[eb.x], s_CoarsePos[eb.y], s_CoarseGrad[eb.x], s_CoarseGrad[eb.y], pb, nb);
        VoxelEdgePoint(s_CoarseDensity[ec.x], s_CoarseDensity[ec.y], s_CoarsePos[ec.x], s_CoarsePos[ec.y], s_CoarseGrad[ec.x], s_CoarseGrad[ec.y], pc, nc);

        float  sd;
        float3 bary;
        if (!RayTriangle(finePos, n, pa, pb, pc, sd, bary) || abs(sd) > radiusMeter)
            continue;

        float3 nFace = normalize(cross(pc - pa, pb - pa)); // emitted winding (i0, i2, i1)
        if (dot(nFace, n) <= 0.0)
        {
            cellFlags |= 2u;
            continue;
        }

        if (abs(sd) < abs(r.sd)) // closest same-facing hit: keep s and its normals together
        {
            r.sd      = sd;
            r.nFace   = nFace;
            r.nTarget = normalize(bary.x * na + bary.y * nb + bary.z * nc);
            r.code    = 0u;
        }
    }
}

// Fine vertex (finePos, n) -> nearest same-facing coarse triangle along n within radiusMeter.
VoxelProjectResult ProjectToCoarseMesh(VoxelTerrainGenParams gp, float3 finePos, float3 n, float radiusMeter, uint stride, StructuredBuffer< float > Density, StructuredBuffer< int > TriTable)
{
    VoxelProjectResult r;
    r.sd      = FLT_MAX;
    r.nTarget = n;
    r.nFace   = n;
    r.code    = 1u;

    // every coarse cell the ray segment [-r, +r] can pass through: the box spanned by its two end points
    uint cellFlags = 0u;
    float3 pA = finePos - radiusMeter * n;
    float3 pB = finePos + radiusMeter * n;
    int3   lo = VoxelCoarseOrigin(gp, min(pA, pB), stride);
    int3   hi = VoxelCoarseOrigin(gp, max(pA, pB), stride);
    [loop] for (int z = lo.z; z <= hi.z; z += stride)
    [loop] for (int y = lo.y; y <= hi.y; y += stride)
    [loop] for (int x = lo.x; x <= hi.x; x += stride)
        ProjectTestCell(gp, int3(x, y, z), finePos, n, radiusMeter, stride, Density, TriTable, r, cellFlags);

    if (r.code != 0u)
    {
        r.code = (cellFlags & 2u) ? 3u : ((cellFlags & 1u) ? 2u : 1u); // back-facing only | no hit within radius | empty cells
        r.sd   = 0.0;
    }
    return r;
}

// Coarse-field gradient direction at pos (trilinear blend of the corner gradients)
float3 VoxelCoarseGradientDir(VoxelTerrainGenParams gp, float3 pos, uint stride, StructuredBuffer<float> Density)
{
    int3 origin = VoxelCoarseOrigin(gp, pos, stride);
    GetCoarseGeometry(gp, origin, stride, Density);

    float3 t   = saturate((pos / gp.voxelSizeMeter - float3(origin)) / float(stride));
    float3 gx0 = lerp(lerp(s_CoarseGrad[0], s_CoarseGrad[1], t.x), lerp(s_CoarseGrad[3], s_CoarseGrad[2], t.x), t.y);
    float3 gx1 = lerp(lerp(s_CoarseGrad[4], s_CoarseGrad[5], t.x), lerp(s_CoarseGrad[7], s_CoarseGrad[6], t.x), t.y);
    float3 g   = lerp(gx0, gx1, t.z);
    float  gl2 = dot(g, g);
    return (gl2 > 1e-12) ? g * rsqrt(gl2) : float3(0.0, 1.0, 0.0);
}

// One geomorph stage: project along the morph ray; when that misses, find the coarse surface along its gradient and keep the part of that offset the ray can express
VoxelProjectResult VoxelProjectStage(VoxelTerrainGenParams gp, float3 pos, float3 n, float radiusMeter, uint stride, StructuredBuffer< float > Density, StructuredBuffer< int > TriTable)
{
    VoxelProjectResult pr1 = ProjectToCoarseMesh(gp, pos, n, radiusMeter, stride, Density, TriTable);
    if (pr1.code != 0u)
    {
        float3 g = VoxelCoarseGradientDir(gp, pos, stride, Density);
        VoxelProjectResult pr2 = ProjectToCoarseMesh(gp, pos, g, radiusMeter, stride, Density, TriTable);
        if (pr2.code == 0u)
        {
            pr1.sd      = pr2.sd * dot(g, n);
            pr1.nTarget = pr2.nTarget;
            pr1.code    = 0u;
        }
    }
    return pr1;
}

#endif // _HLSL_VOXEL_TERRAIN_COMMON_HEADER
