#ifndef _HLSL_SAMPLING_HEADER
#define _HLSL_SAMPLING_HEADER

#include "HelperFunctions.hlsli"

// PCG Hash (O'Neill 2014)
uint PCGHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}


// Stateful RNG - wraps PCGHash into a NextFloat() interface
struct RngState
{
    uint seed;     // base seed, constant for the whole path
    uint counter;  // dimension counter, advanced by each NextFloat call
};


// Build an RNG state for a given (pixel, frame, sample) triple.
RngState InitRng(uint2 pixel, uint frameIndex, uint sampleIndex)
{
    // Stage 1: pack the 2D pixel into one uint (resolution <= 65536).
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


// Return a uniform [0, 1) sample and advance the RNG to the next dimension.
float NextFloat(inout RngState rng)
{
    // Hash (seed + counter), not (state after counter rounds of PCG):
    // the additive form makes random access to any dimension O(1), which
    // we rely on for the Sobol migration in Phase 7.
    uint hashed = PCGHash(rng.seed + rng.counter);
    rng.counter++;

    // Map uint32 -> float in [0, 1).
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


// Return two uniform [0, 1) samples. Use this when you need a 2D random point
float2 NextFloat2(inout RngState rng)
{
    float a = NextFloat(rng);
    float b = NextFloat(rng);
    return float2(a, b);
}

float SamplePixelTent1D(float u)
{
    // Triangular sub-pixel filter with support inside the current pixel.
    return (u < 0.5)
        ? 0.5 * (sqrt(2.0 * u) - 1.0)
        : 0.5 * (1.0 - sqrt(2.0 - 2.0 * u));
}

float2 SamplePixelTent(float2 u)
{
    return float2(SamplePixelTent1D(u.x), SamplePixelTent1D(u.y));
}


// Branchless Orthonormal Basis (Duff et al. 2017, "Building an Orthonormal Basis, Revisited", JCGT 6(1), 1-8)
void BuildONB(float3 n, out float3 t, out float3 b)
{
    // `sign` captures the hemisphere of n.z without a branch.
    const float sign = (n.z >= 0.0) ? 1.0 : -1.0;

    const float a = -1.0 / (sign + n.z);
    const float h = n.x * n.y * a;

    t = float3(1.0 + sign * n.x * n.x * a,
               sign * h,
               -sign * n.x);

    b = float3(h,
               sign + n.y * n.y * a,
               -n.y);
}


// Spherical coordinate helpers
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

void SampleHemisphere_Cosine(float2 u, out float3 dir, out float pdf)
{
    const float phi = 2 * PI * u.y;

    const float sinTheta = safeSqrt(u.x);
    const float cosTheta = safeSqrt(1 - u.x);

    dir = SphericalToCartesian(cosTheta, sinTheta, phi);
    pdf = cosTheta / PI; // cosine-weighted pdf
}


// Area / Sphere light surface samplers
void SampleAreaLight(float2 u, AreaLight light, out float3 y, out float3 normalAtY, out float pdfA)
{
    float3 position  = float3(light.posX,     light.posY,     light.posZ);
    float3 tangent   = float3(light.tangentX, light.tangentY, light.tangentZ);
    float3 normal    = float3(light.normalX,  light.normalY,  light.normalZ);
    float3 bitangent = cross(tangent, normal);

    float2 s = u * 2.0 - 1.0;

    y         = position + (s.x * light.halfWidth) * tangent
                         + (s.y * light.halfHeight) * bitangent;
    normalAtY = normal;
    pdfA      = 1.0 / (4.0 * light.halfWidth * light.halfHeight);
}

void SampleDiskLight(float2 u, DiskLight light, out float3 y, out float3 normalAtY, out float pdfA)
{
    float3 position  = float3(light.posX,     light.posY,     light.posZ);
    float3 tangent   = float3(light.tangentX, light.tangentY, light.tangentZ);
    float3 normal    = float3(light.normalX,  light.normalY,  light.normalZ);
    float3 bitangent = cross(tangent, normal);

    float r   = light.radius * safeSqrt(u.x);
    float phi = 2.0 * PI * u.y;
    float2 d  = float2(r * cos(phi), r * sin(phi));

    y         = position + d.x * tangent + d.y * bitangent;
    normalAtY = normal;
    pdfA      = 1.0 / max(PI * light.radius * light.radius, EPSILON_MIN);
}

void SampleSphereLight(float2 u, SphereLight light, out float3 y, out float3 normalAtY, out float pdfA)
{
    float3 position = float3(light.posX, light.posY, light.posZ);

    float cosTheta = 1.0 - 2.0 * u.x;
    float sinTheta = safeSqrt(1.0 - cosTheta * cosTheta);
    float phi      = 2.0 * PI * u.y;

    float3 dir = float3(sinTheta * cos(phi),
                        sinTheta * sin(phi),
                        cosTheta);

    y         = position + light.radius * dir;
    normalAtY = dir; // sphere outward normal at the surface point
    pdfA      = 1.0 / (4.0 * PI * light.radius * light.radius);
}

void SampleTubeLight(float2 u, TubeLight light, out float3 y, out float3 normalAtY, out float pdfA)
{
    float3 a = float3(light.posAX, light.posAY, light.posAZ);
    float3 b = float3(light.posBX, light.posBY, light.posBZ);
    float3 axisVec = b - a;
    float  lengthTube = length(axisVec);
    if (lengthTube <= EPSILON_MIN || light.radius <= 0.0)
    {
        y = a;
        normalAtY = float3(0.0, 1.0, 0.0);
        pdfA = 0.0;
        return;
    }

    float3 axis = axisVec / lengthTube;
    float3 tangent;
    float3 bitangent;
    BuildONB(axis, tangent, bitangent);

    float phi = 2.0 * PI * u.x;
    float3 radial = cos(phi) * tangent + sin(phi) * bitangent;
    float t = u.y;

    y = lerp(a, b, t) + light.radius * radial;
    normalAtY = radial;
    pdfA = 1.0 / max(2.0 * PI * light.radius * lengthTube, EPSILON_MIN);
}


// Analytic light ray-intersection
bool IntersectRayAreaLight(float3 origin, float3 dir, AreaLight light, out float tHit, out float3 normalAtHit)
{
    float3 lpos    = float3(light.posX,     light.posY,     light.posZ);
    float3 ltan    = float3(light.tangentX, light.tangentY, light.tangentZ);
    float3 lnorm   = float3(light.normalX,  light.normalY,  light.normalZ);
    float3 lbitan  = cross(ltan, lnorm);

    // Single-sided: ray must approach from the front hemisphere
    // (i.e. dot(lnorm, -dir) > 0  =>  dot(lnorm, dir) < 0).
    float denom = dot(lnorm, dir);
    if (denom >= -1e-6) { tHit = -1.0; normalAtHit = float3(0, 0, 0); return false; }

    // Plane intersection: t = dot(lpos - origin, lnorm) / dot(lnorm, dir).
    float t = dot(lpos - origin, lnorm) / denom;
    if (t <= 0.001) { tHit = -1.0; normalAtHit = float3(0, 0, 0); return false; }

    // Bounds check in the rectangle's tangent / bitangent frame.
    float3 p = origin + t * dir;
    float3 d = p - lpos;
    float  u = dot(d, ltan);
    float  v = dot(d, lbitan);
    if (abs(u) > light.halfWidth || abs(v) > light.halfHeight)
    {
        tHit = -1.0; normalAtHit = float3(0, 0, 0); return false;
    }

    tHit        = t;
    normalAtHit = lnorm;
    return true;
}

bool IntersectRaySphereLight(float3 origin, float3 dir, SphereLight light, out float tHit, out float3 normalAtHit)
{
    float3 c  = float3(light.posX, light.posY, light.posZ);
    float3 oc = origin - c;
    float  b  = dot(oc, dir);                              // half-b form
    float  cc = dot(oc, oc) - light.radius * light.radius;
    float  disc = b * b - cc;
    if (disc < 0.0) { tHit = -1.0; normalAtHit = float3(0, 0, 0); return false; }

    float s = sqrt(disc);
    float t = -b - s;              // entry point (front face for external rays)
    if (t <= 0.001) t = -b + s;    // origin inside the sphere -> exit point
    if (t <= 0.001) { tHit = -1.0; normalAtHit = float3(0, 0, 0); return false; }

    float3 p = origin + t * dir;
    tHit        = t;
    normalAtHit = normalize(p - c);
    return true;
}


// BSDF pdf evaluator for cross-evaluation
float EvaluateBSDFPdf(float3 dir, float3 n, uint strategy)
{
    float c = dot(dir, n);
    if (c <= 0.0) 
        return 0.0;

    if (strategy == 1u)
        return c / PI;
    else
        return 1.0 / (2.0 * PI);
}


#endif // _HLSL_SAMPLING_HEADER
