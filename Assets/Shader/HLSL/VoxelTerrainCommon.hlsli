#ifndef _HLSL_VOXEL_TERRAIN_COMMON_HEADER
#define _HLSL_VOXEL_TERRAIN_COMMON_HEADER

#include "NoiseCommon.hlsli"

// ---- Gen params + base noise ---------------------------------------------------

// SDF generation parameters (solid < 0, air > 0).
struct VoxelTerrainGenParams
{
    float originX, originY, originZ;
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
    float surfaceLevelRatio;   // base surface height as chunk fraction (0..1)
    float erosionScale;

    float erosionStrength;
    float erosionGullyWeight;
    float erosionDetail;
    float erosionOnsetInput;

    float erosionOnsetOctave;
    float erosionCellScale;
    float erosionNormalization;
    float erosionSlopeScale;

    uint  erosionOctaves;      // 0 = erosion off
    uint  padding0;
    uint  padding1;
    uint  padding2;
};

float3 VoxelTexelToWorld(VoxelTerrainGenParams gp, uint3 texel)
{
    float3 origin     = float3(gp.originX, gp.originY, gp.originZ);
    float3 localIndex = float3(int3(texel) - int(gp.apron));
    return origin + localIndex * gp.voxelSizeMeter;
}

float2 VoxelRotScale(float2 p, float s)
{
    return float2(0.8 * p.x - 0.6 * p.y, 0.6 * p.x + 0.8 * p.y) * s;
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
    const float2 k = float2(0.3183099, 0.3678794); // (1/PI, e^-1)
    x = (x + float(seed) * float2(0.06711056, 0.00583715)) * k + k.yx;
    float t = frac(x.x * x.y * (x.x + x.y));
    return -1.0 + 2.0 * frac(16.0 * k * t);
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

// Erosion style constants.
static const float kEroRoundingRidge    = 0.1;  // ridge rounding (lerp hi endpoint)
static const float kEroRoundingCrease   = 0.0;  // crease rounding (lerp lo endpoint; 0 = sharp creases)
static const float kEroRoundingInput    = 0.1;  // input-feature rounding scale
static const float kEroRoundingOctave   = 2.0;  // per-octave rounding multiplier
static const float kEroOnsetRidgeIn     = 2.8;  // ridge-map input mask onset
static const float kEroOnsetRidgeOct    = 1.5;  // ridge-map octave mask onset
static const float kEroAssumedSlopeMag  = 0.7;  // pretend input slope magnitude (straight-gullies aid)
static const float kEroAssumedSlopeBlend = 1.0; // 0 = real slope, 1 = fully assumed
static const float kEroGain             = 0.5;  // per-octave strength decay
static const float kEroLacunarity       = 2.0;  // per-octave frequency step

float4 VoxelErosionFilterEx(VoxelTerrainGenParams gp, float2 p, float3 heightAndSlope, float fadeTarget,
                            float geoMinWavelengthM, float outMinWavelengthM,
                            out float ridgeMap, out float3 geoDelta)
{
    float strength    = gp.erosionStrength * gp.erosionScale;
    float freq        = 1.0 / max(gp.erosionScale * gp.erosionCellScale, 1e-4);
    float slopeLength = max(length(heightAndSlope.yz), 1e-6);

    fadeTarget = clamp(fadeTarget, -1.0, 1.0);

    float3 inputHeightAndSlope = heightAndSlope;
    float  magnitude    = 0.0;
    float  roundingMult = 1.0;

    float roundingForInput = lerp(kEroRoundingCrease, kEroRoundingRidge,
                                  saturate(fadeTarget + 0.5)) * kEroRoundingInput;
    // combiMask: accumulating slope mask (input slope first, then each octave)
    float combiMask = VoxelEaseOut(VoxelSmoothStart(slopeLength * gp.erosionOnsetInput,
                                                    roundingForInput * gp.erosionOnsetInput));

    // Ridge map: parallel copies of fadeTarget and mask.
    float ridgeMapCombiMask  = VoxelEaseOut(slopeLength * kEroOnsetRidgeIn);
    float ridgeMapFadeTarget = fadeTarget;

    // Initial gully-direction slope: mix of the actual slope and an assumed-magnitude slope.
    float2 gullySlope = lerp(heightAndSlope.yz,
                             heightAndSlope.yz / slopeLength * kEroAssumedSlopeMag,
                             kEroAssumedSlopeBlend);

    bool geoRecorded = false;
    geoDelta = float3(0.0, 0.0, 0.0);

    for (uint i = 0u; i < gp.erosionOctaves; ++i)
    {
        // band limit: octaves finer than the consumer grid only alias
        float wavelength = 1.0 / (freq * gp.erosionCellScale);
        if (!geoRecorded && wavelength < geoMinWavelengthM)
        {
            geoDelta    = heightAndSlope - inputHeightAndSlope; // geometry-band snapshot
            geoRecorded = true;
        }
        if (wavelength < outMinWavelengthM)
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
        float roundingForOctave = lerp(kEroRoundingCrease, kEroRoundingRidge,
                                       saturate(phacelle.x + 0.5)) * roundingMult;
        float newMask = VoxelEaseOut(VoxelSmoothStart(sloping * gp.erosionOnsetOctave,
                                                      roundingForOctave * gp.erosionOnsetOctave));
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

// Geometry band limit (grid Nyquist)
float VoxelErosionGeoMinWavelength(VoxelTerrainGenParams gp)
{
    return 2.0 * gp.voxelSizeMeter;
}

// Geometry-band wrapper: evaluates down to the geometry wavelength only
float4 VoxelErosionFilter(VoxelTerrainGenParams gp, float2 p, float3 heightAndSlope, float fadeTarget, out float ridgeMap)
{
    float  minWavelength = VoxelErosionGeoMinWavelength(gp);
    float3 geoUnused;
    return VoxelErosionFilterEx(gp, p, heightAndSlope, fadeTarget, minWavelength, minWavelength, ridgeMap, geoUnused);
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
    float  chunkSize = float(gp.cellsPerAxis) * gp.voxelSizeMeter;
    float3 origin    = float3(gp.originX, gp.originY, gp.originZ);

    float h01        = VoxelTerrainErodedHeight01(gp, worldPos.xz);
    float baseY      = origin.y + gp.surfaceLevelRatio * chunkSize;
    float surfaceY   = baseY + (h01 - 0.5) * gp.mountainAmplitude;
    float surfaceSDF = worldPos.y - surfaceY; // solid (<0) below the surface

    // chunk cube bound (full extent) so a single chunk is a closed solid
    float3 center   = origin + 0.5 * chunkSize;
    float3 q        = abs(worldPos - center) - 0.5 * chunkSize;
    float  chunkSDF = length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);

    return max(surfaceSDF, chunkSDF); // intersection: below surface AND inside chunk
}

// ---- Dicing helpers --------------------------------------------------------------

// Base tris per MS group by budget level Lm; levels >= 4 dice one child per group.
static const uint kDiceTrisPerGroup[6] = { 21u, 10u, 4u, 1u, 1u, 1u };

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
    return (uint)clamp(lm, 1.0, (float)min(desc.diceMaxLevel, 5u));
}

// MS group count one payload slot needs at budget level
uint DiceGroupsForMeshlet(uint lm, uint triCount)
{
    if (lm <= 3u)
    {
        uint t = kDiceTrisPerGroup[lm];
        return (triCount + t - 1u) / t;
    }
    return triCount << (2u * (lm - 3u)); // triCount * 4^(lm-3)
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
uint3 DiceSnapSubVertexCoord(uint3 coord, uint Lt, uint3 le, float3 p0L, float3 p1L, float3 p2L)
{
    uint n = 1u << Lt;

    if (coord.z == 0u && coord.x != 0u && coord.y != 0u)      // on edge p0-p1, k runs p0 -> p1
    {
        uint k = DiceSnapEdgeK(coord.y, Lt, le.x, DiceLexLess(p0L, p1L));
        coord  = uint3(n - k, k, 0u);
    }
    else if (coord.x == 0u && coord.y != 0u && coord.z != 0u) // on edge p1-p2, k runs p1 -> p2
    {
        uint k = DiceSnapEdgeK(coord.z, Lt, le.y, DiceLexLess(p1L, p2L));
        coord  = uint3(0u, n - k, k);
    }
    else if (coord.y == 0u && coord.z != 0u && coord.x != 0u) // on edge p2-p0, k runs p2 -> p0
    {
        uint k = DiceSnapEdgeK(coord.x, Lt, le.z, DiceLexLess(p2L, p0L));
        coord  = uint3(k, 0u, n - k);
    }
    return coord;
}

// subdivided(diced) vertices.
void DiceChildCorners(uint child, uint Lt, out uint3 cc0, out uint3 cc1, out uint3 cc2)
{
    uint  lc  = Lt - 3u;
    uint3 sub = DiceSubTriVerts(child, lc);

    cc0 = DiceSubVertexCoordInt(sub.x, lc);
    cc1 = DiceSubVertexCoordInt(sub.y, lc);
    cc2 = DiceSubVertexCoordInt(sub.z, lc);
}

uint3 DiceHierCoord(uint3 cc0, uint3 cc1, uint3 cc2, uint3 localCoord)
{
    return localCoord.x * cc0 + localCoord.y * cc1 + localCoord.z * cc2;
}

// Sub-vertex -> chunk-local position/normal: snap, then one barycentric lerp.
void DiceSubVertex(uint3 coord, uint Lt, uint3 le,
                   float3 p0L, float3 p1L, float3 p2L,
                   float3 n0,  float3 n1,  float3 n2,
                   out float3 posL, out float3 nrmL)
{
    coord = DiceSnapSubVertexCoord(coord, Lt, le, p0L, p1L, p2L);

    precise float  invN = 1.0 / (float)(1u << Lt);
    precise float3 b    = float3(coord) * invN;
    precise float3 p    = b.x * p0L + b.y * p1L + b.z * p2L;
    posL = p;
    nrmL = b.x * n0 + b.y * n1 + b.z * n2;
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

// Micro band height + world-space slope: returns (h [m], dh/dx, dh/dz) at xz for
// receiver LOD lv.
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
        v += sgnS * abs(s) * 0.2424; // zero-center the fold: E[2|n|-1] = -0.2424 for this value noise
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

float3 DisplaceVoxelDice(float3 posWS, float baseNy, float3 camPosWS, VoxelChunkDesc chunk, Texture2D< float4 > ErosionMap, SamplerState Sampler)
{
    float2 uv      = (posWS.xz - float2(chunk.originX, chunk.originZ)) / max(chunk.chunkSizeMeter, 1e-3);
    float4 erosion = ErosionMap.SampleLevel(Sampler, uv, 0); // R detail height (m) | G ridgeMap | B surfaceY | A unused

    float dCam      = length(posWS - camPosWS);
    float wDist     = saturate((chunk.diceRadiusMeter - dCam) / chunk.diceFadeWidthMeter); // fade out at the dicing radius
    float hfGate    = saturate(1.0 - abs(posWS.y - erosion.b) / 2.0);              // height-field gate, +/-2 m tolerance
    float slopeGate = saturate(baseNy);                                            // no displacement on steep slopes (avoid self-intersection)

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

#endif // _HLSL_VOXEL_TERRAIN_COMMON_HEADER
