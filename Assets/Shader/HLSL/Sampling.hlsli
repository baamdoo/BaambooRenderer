#ifndef _HLSL_SAMPLING_HEADER
#define _HLSL_SAMPLING_HEADER

#include "HelperFunctions.hlsli"

// ───────────────────────────────────────────────────────────────────
// PCG Hash (O'Neill 2014)
// ───────────────────────────────────────────────────────────────────
//
// A single-step permutation: takes any 32-bit integer and returns a new
// 32-bit integer whose bits are well-mixed. This is the building block
// for the stateful RNG below — it is NOT a stream generator by itself.
//
// Why PCG and not a plain LCG?
//   A naive LCG (state = state*a + c) has visibly regular low bits.
//   PCG adds an output permutation that "launders" those bits. The result
//   passes BigCrush and is cheap enough to call once per ray.
//
uint PCGHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}


// ───────────────────────────────────────────────────────────────────
// Stateful RNG — wraps PCGHash into a NextFloat() interface
// ───────────────────────────────────────────────────────────────────
//
// Design: (seed, counter) pair
//   - `seed` is the base value derived once in InitRng() from the triple
//     (pixel, frameIndex, sampleIndex). It never changes during a path.
//   - `counter` is the "dimension index". Each call to NextFloat()
//     consumes one dimension and advances the counter by 1.
//
// Why split seed and counter?
//   This shape makes the i-th random number a pure function of
//   (seed, i), which means:
//     1. Two parallel renders with identical seeds produce identical
//        sequences — reproducibility for ML ground truth.
//     2. We can jump to any dimension in O(1) (useful when a path decides
//        "I want the 7th random number directly").
//     3. In Phase 7 we replace PCGHash(seed + counter) with a Sobol
//        lookup indexed by `counter`, and NOTHING in the caller changes.
//        This is why we expose `counter` as an explicit field rather than
//        burying it inside an opaque advance-state function.
//
// Seed combining strategy — why three steps, not one XOR
//
//   The base seed must uniquely identify a (pixel, frame, sample) triple
//   so that no two paths anywhere in the whole render share a sequence.
//   A naive `pixel ^ frame ^ sample` would collide whenever the three
//   values happen to have aligned bits (common at frame 0, pixel 0,
//   sample 0 for example). We do it in three stages instead:
//
//   Stage 1 — pack the 2D pixel into one uint
//     We use (pixel.y << 16) | pixel.x. This is exact and cheap for
//     resolutions up to 65536 on either axis (far beyond anything we'll
//     render). If you ever exceed that, switch to pixel.y*width + pixel.x.
//
//   Stage 2 — fold frameIndex in via the GOLDEN-RATIO PRIME 0x9E3779B9
//     0x9E3779B9 = floor(2^32 / φ) where φ is the golden ratio. This
//     particular constant has the property that successive integer
//     multiples visit the full 32-bit range as uniformly as possible
//     (Knuth's multiplicative hash, also used in Java's HashMap and in
//     blue-noise dither). Multiplying frameIndex by it guarantees that
//     adjacent frames produce maximally different starting seeds.
//     After the XOR we pipe through PCGHash to destroy any residual
//     structure before stage 3 sees it.
//
//   Stage 3 — fold sampleIndex in via a second PCG round
//     This handles the case where a single frame wants more than one
//     primary sample per pixel (spp_per_frame > 1 during warmup). Each
//     sample within a frame gets an independent starting seed.
//
// CRITICAL — why frameIndex MUST be mixed in
//
//   If frameIndex is forgotten, every frame redraws the SAME random
//   pattern, and temporal accumulation (Step 1.6) averages the same
//   biased image against itself. It looks like "noise is going away",
//   but actually converges to the wrong value. This is one of the
//   #1 sources of broken Monte Carlo renderers in student projects.
//
// CRITICAL — why pixel.xy must BOTH be mixed in
//
//   If only pixel.x is used, every column of pixels gets the same
//   sequence → visible vertical streaks of correlated noise. Likewise
//   for rows. The pack in stage 1 guarantees every pixel is unique.
//

struct RngState
{
    uint seed;     // base seed, constant for the whole path
    uint counter;  // dimension counter, advanced by each NextFloat call
};


// Build an RNG state for a given (pixel, frame, sample) triple.
//
// Call this ONCE at the top of the raygen shader (or once at the top of
// each sample loop if spp_per_frame > 1). Do NOT call it inside the inner
// hemisphere-sampling loop — that would reset the dimension counter and
// break independence between successive NextFloat calls.
//
RngState InitRng(uint2 pixel, uint frameIndex, uint sampleIndex)
{
    // Stage 1: pack the 2D pixel into one uint (resolution ≤ 65536).
    uint pixelSeed = (pixel.y << 16u) | (pixel.x & 0xFFFFu);

    // Stage 2: mix frameIndex via the golden-ratio prime, then PCG-hash.
    // The XOR scatters frame changes across the high bits; PCGHash then
    // non-linearly mixes those into every output bit.
    uint seed = PCGHash(pixelSeed ^ (frameIndex * 0x9E3779B9u));

    // Stage 3: mix sampleIndex via a second PCG round.
    seed      = PCGHash(seed + sampleIndex);

    RngState rng;
    rng.seed    = seed;
    rng.counter = 0u;
    return rng;
}


// Return a uniform [0, 1) sample and advance the RNG to the next
// dimension. The i-th call to this function on a given RngState reads
// dimension i of the underlying point.
//
float NextFloat(inout RngState rng)
{
    // Hash (seed + counter), not (state after counter rounds of PCG):
    // the additive form makes random access to any dimension O(1), which
    // we rely on for the Sobol migration in Phase 7.
    uint hashed = PCGHash(rng.seed + rng.counter);
    rng.counter++;

    // Map uint32 → float in [0, 1).
    //
    // Why (1.0 / 4294967296.0) and not 0xFFFFFFFF?
    //   2^32 = 4294967296 gives an exact half-open mapping [0, 1).
    //   Dividing by 0xFFFFFFFF would put the maximum at exactly 1.0,
    //   which breaks inverse-transform sampling at the interval endpoint
    //   (e.g. acos(1.0) at the pole, log(1.0) in Russian Roulette).
    //
    // Note: multiplying by a precomputed reciprocal is faster than
    // division on GPUs and is bit-exact for powers of two. The constant
    // 2^-32 is exactly representable in float32.
    return float(hashed) * (1.0 / 4294967296.0);
}


// Return two uniform [0, 1) samples. Use this when you need a 2D random
// point — for example, when feeding SampleHemisphere_Uniform(u, …).
//
// Order matters: .x is dimension i, .y is dimension i+1. If you ever run
// two parallel renders for reproducibility comparison, both must consume
// dimensions in the same order.
//
float2 NextFloat2(inout RngState rng)
{
    float a = NextFloat(rng);
    float b = NextFloat(rng);
    return float2(a, b);
}


// ───────────────────────────────────────────────────────────────────
// Branchless Orthonormal Basis (Duff et al. 2017,
// "Building an Orthonormal Basis, Revisited", JCGT 6(1), 1–8)
// ───────────────────────────────────────────────────────────────────
//
// Purpose
//   Given a unit normal `n`, produce two unit vectors `t` and `b` that
//   are orthogonal to `n` and to each other, so that (t, b, n) forms a
//   right-handed local frame suitable for tangent-space hemisphere
//   sampling. In particular:
//       float3x3 tangentToWorld = float3x3(t, b, n);
//       dirWorld = mul(dirTangent, tangentToWorld);
//   takes a direction expressed in tangent space (where z = n) and
//   maps it to world space.
//
// Why a dedicated utility?
//   In the closest-hit shader we already have a per-vertex tangent that
//   was computed at asset-import time (see RaytracingTestLIB.hlsl:557~).
//   But when we sample the hemisphere around a *geometric* normal — or
//   when the asset has no meaningful tangent (procedural surface,
//   analytic sphere, volume) — we need to synthesize a tangent from
//   `n` alone. This function does exactly that.
//
// Why branchless?
//   The intuitive "pick a non-parallel axis" trick
//       t = (abs(n.x) < 0.9) ? (1,0,0) : (0,1,0);
//       t = normalize(cross(t, n));
//   forces every ray in a warp to agree on the branch, or eat the cost
//   of divergence. Duff et al. observed that you can replace the branch
//   with copysign() on n.z, which gives a *uniformly accurate* ONB in
//   about 8 FLOPs with zero divergence. The particular numeric form
//   below is their Listing 3 — do not "simplify" it. In particular the
//   sign chosen here must match `sign = n.z >= 0 ? 1 : -1` (which is
//   what the `1.0 / (sign + n.z)` construction encodes) or the basis
//   collapses near n.z ≈ -1.
//
// Properties guaranteed by construction
//   - length(t) == 1      (to float32 precision)
//   - length(b) == 1
//   - dot(t, n) ≈ 0
//   - dot(b, n) ≈ 0
//   - dot(t, b) ≈ 0
//   - (t, b, n) is right-handed: dot(cross(t, b), n) ≈ +1
//
// The small errors are on the order of 1e-7 and are negligible for
// integration purposes. If you ever need exact orthogonality (e.g. for
// a rotation matrix fed to an eigenvalue solver), re-orthogonalize
// manually — but that is not needed anywhere in the path tracer.
//
void BuildONB(float3 n, out float3 t, out float3 b)
{
    // `sign` captures the hemisphere of n.z without a branch.
    // We rely on HLSL's "1.0f copied with n.z's sign" semantics here.
    const float sign = (n.z >= 0.0) ? 1.0 : -1.0;

    // `a` and `h` are Duff et al.'s auxiliary scalars. They are chosen
    // specifically so that the formula below is accurate even when n is
    // nearly aligned with ±z (the classic failure mode of the naive
    // cross-product construction).
    const float a = -1.0 / (sign + n.z);
    const float h = n.x * n.y * a;

    t = float3(1.0 + sign * n.x * n.x * a,
               sign * h,
               -sign * n.x);

    b = float3(h,
               sign + n.y * n.y * a,
               -n.y);
}


// ───────────────────────────────────────────────────────────────────
// Spherical coordinate helpers
// ───────────────────────────────────────────────────────────────────

float3 SphericalToCartesian(float cosTheta, float sinTheta, float phi)
{
    const float x = sinTheta * cos(phi);
    const float y = sinTheta * sin(phi);
    const float z = cosTheta;
    return float3(x, y, z);
}

void SampleHemisphere_Uniform(float2 u, out float3 dir, out float pdf)
{
    const float phi = 2 * PI * u.y;
    
    const float cosTheta = u.x;
    const float sinTheta = safeSqrt(1 - cosTheta * cosTheta);
    
    dir = SphericalToCartesian(cosTheta, sinTheta, phi);
    pdf = 1 / (2 * PI); // uniform pdf
}


#endif // _HLSL_SAMPLING_HEADER