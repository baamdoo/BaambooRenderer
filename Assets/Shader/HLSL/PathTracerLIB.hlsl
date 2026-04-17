#define _CAMERA
#define _MESH
#include "Common.hlsli"
#include "Sampling.hlsli"


// ───────────────────────────────────────────────────────────────────
// Bindings
// ──────────────────────────────────────────────────────────────────-
cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_FrameIndex;      // monotonically increasing frame counter (RNG seed)
    uint  g_AccumReset;      // 1 = overwrite accum with current sample; 0 = add
    uint  g_NumSamples;      // samples per pixel this frame (start with 16)
    uint  g_FurnaceMode;     // 1 = ignore skybox, use constant L_env; 0 = real sky
    float g_FurnaceLenvR;    // channel-specific constant sky radiance (furnace)
    float g_FurnaceLenvG;
    float g_FurnaceLenvB;
    float g_TestAlbedo;      // Phase 1 constant Lambertian reflectance (ρ). See below.
};

ConstantBuffer< DescriptorHeapIndex > g_Skybox      : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_AccumBuffer : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Display     : register(b3, ROOT_CONSTANT_SPACE);

RaytracingAccelerationStructure g_Scene : register(t0, space1);


// ───────────────────────────────────────────────────────────────────
// Ray types & payloads
// ───────────────────────────────────────────────────────────────────
// RAY_TYPE_PRIMARY : fired from RayGen, computes final radiance for a pixel by integrating over the hemisphere.
// RAY_TYPE_SKY     : fired from inside the hemisphere loop. 
//
// Two payload structs so the two ray types cannot accidentally be
// routed through the wrong shader table slot. The SBT assigns an
// independent hit group and miss shader to each.
#define RAY_TYPE_PRIMARY 0
#define RAY_TYPE_SKY     1
#define NUM_RAY_TYPES    2

struct PrimaryPayload
{
    float3 radiance;       // final integrated radiance for this pixel
};

struct SkyPayload
{
    float3 radiance;       // L_i returned from a sky-ray miss / hit
};


// ───────────────────────────────────────────────────────────────────
// Helpers
// ───────────────────────────────────────────────────────────────────
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

float3 OffsetRay(float3 p, float3 n)
{
    return p + n * 0.001;
}

float3 SampleSkyRadiance(float3 origin, float3 dir)
{
    SkyPayload p;
    p.radiance = float3(0, 0, 0);

    RayDesc r;
    r.Origin    = origin;
    r.Direction = dir;
    r.TMin      = 0.001;
    r.TMax      = g_Camera.zFar;

    TraceRay(
        g_Scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF,
        RAY_TYPE_SKY,
        NUM_RAY_TYPES,
        RAY_TYPE_SKY,
        r,
        p);

    return p.radiance;
}

// Fetch the "environment radiance" for a given direction. This is what
// the sky miss shader uses and what a hemisphere sample converges to
// in the furnace test.
float3 EvaluateSkyRadiance(float3 dir)
{
    if (g_FurnaceMode != 0)
    {
        return float3(g_FurnaceLenvR, g_FurnaceLenvG, g_FurnaceLenvB);
    }

    TextureCube< float3 > Skybox = GetResource(g_Skybox.index);
    return Skybox.SampleLevel(g_LinearClampSampler, dir, 0);
}


// ───────────────────────────────────────────────────────────────────
// Ray Generation — primary ray + accumulation write-back
// ───────────────────────────────────────────────────────────────────
//
// Pipeline:
//   1. Reconstruct a primary ray through this pixel's NDC position.
//   2. Trace it. The closesthit shader fills payload.radiance with the
//      integrated L_o for this frame's sample(s).
//   3. Blend that into the persistent accumulation buffer:
//        - if AccumReset flag set (camera moved): overwrite.
//        - otherwise: add to running sum, increment count.
//   4. Write the display texture: sum / count.
//
// Storage convention for accumBuffer texels:
//   .xyz = running sum of radiance (linear, unbounded)
//   .w   = running sample count (float; exact up to 2^24, fine for
//          several thousand frames of accumulation — well past furnace-test convergence)

[shader("raygeneration")]
void RayGen()
{
    RWTexture2D< float4 > AccumBuf = GetResource(g_AccumBuffer.index);
    RWTexture2D< float4 > Display  = GetResource(g_Display.index);

    uint2 rayIndex      = DispatchRaysIndex().xy;
    uint2 rayDimensions = DispatchRaysDimensions().xy;

    // NDC → world ray. Reverse-Z: near plane is at clip depth 1.0
    float2 uv        = (float2(rayIndex) + 0.5) / float2(rayDimensions);
    float3 target    = ReconstructWorldPos(uv, 0.0, g_Camera.mViewProjInv);
    float3 rayDir    = normalize(target - g_Camera.posWORLD);
    float3 rayOrigin = g_Camera.posWORLD;

    RayDesc ray;
    ray.Origin    = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin      = g_Camera.zNear;
    ray.TMax      = g_Camera.zFar;

    PrimaryPayload payload;
    payload.radiance = float3(0, 0, 0);

    TraceRay(
        g_Scene,
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
        0xFF,
        RAY_TYPE_PRIMARY,
        NUM_RAY_TYPES,
        RAY_TYPE_PRIMARY,
        ray,
        payload);

    float3 L = payload.radiance;
    if (any(isnan(L)) || any(isinf(L)))
    {
        L = float3(0, 0, 0);
    }

    // Blend into accumulation buffer.
    float4 prev = AccumBuf[rayIndex];
    float3 newSum;
    float  newCnt;

    if (g_AccumReset != 0)
    {
        newSum = L;
        newCnt = 1.0;
    }
    else
    {
        newSum = prev.xyz + L;
        newCnt = prev.w + 1.0;
    }

    AccumBuf[rayIndex] = float4(newSum, newCnt);
    Display[rayIndex]  = float4(newSum / max(newCnt, 1.0), 1.0);
}


// ───────────────────────────────────────────────────────────────────
// Primary Miss — primary ray hit nothing, return sky
// ───────────────────────────────────────────────────────────────────
[shader("miss")]
void PrimaryMiss(inout PrimaryPayload payload)
{
    float3 dir = normalize(WorldRayDirection());
    payload.radiance = EvaluateSkyRadiance(dir);
}


// ───────────────────────────────────────────────────────────────────
// Sky Miss — hemisphere-loop child ray hit nothing, return sky
// ───────────────────────────────────────────────────────────────────
[shader("miss")]
void SkyMiss(inout SkyPayload payload)
{
    float3 dir = normalize(WorldRayDirection());
    payload.radiance = EvaluateSkyRadiance(dir);
}


// ───────────────────────────────────────────────────────────────────
// Sky AnyHit — child ray hit a surface. For now, this means 
// "that direction is blocked — L_i from that direction is zero."
// ───────────────────────────────────────────────────────────────────
[shader("anyhit")]
void SkyAnyHit(inout SkyPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.radiance = float3(0, 0, 0);
    AcceptHitAndEndSearch();
}


// ───────────────────────────────────────────────────────────────────
// Primary Closest Hit
// ───────────────────────────────────────────────────────────────────
// Hemisphere integral to be computed:
//
//     L_o(x, V) ≈ (1/N) · Σᵢ [ f_r · L_i(ωᵢ) · cos(θᵢ) ] / p(ωᵢ)

[shader("closesthit")]
void ClosestHit_Primary(inout PrimaryPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // --- Vertex unpacking ---------------------------------------------------
    StructuredBuffer< MeshData >     Meshes      = GetResource(g_Meshes.index);
    StructuredBuffer< InstanceData > Instances   = GetResource(g_Instances.index);
    StructuredBuffer< uint >         IndexBuffer = GetResource(g_Indices.index);
    StructuredBuffer< Vertex >       VertexBuf   = GetResource(g_Vertices.index);

    InstanceData instance = Instances[InstanceID()];
    MeshData     mesh     = Meshes[instance.meshID];

    uint primIdx    = PrimitiveIndex();
    uint indexBase  = mesh.lods[0].iOffset + primIdx * 3;
    uint i0 = IndexBuffer[indexBase + 0];
    uint i1 = IndexBuffer[indexBase + 1];
    uint i2 = IndexBuffer[indexBase + 2];

    Vertex v0 = VertexBuf[mesh.vOffset + i0];
    Vertex v1 = VertexBuf[mesh.vOffset + i1];
    Vertex v2 = VertexBuf[mesh.vOffset + i2];

    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y,
                         attr.barycentrics.x,
                         attr.barycentrics.y);

    float3 normalOS =
        float3(v0.normalX, v0.normalY, v0.normalZ) * bary.x +
        float3(v1.normalX, v1.normalY, v1.normalZ) * bary.y +
        float3(v2.normalX, v2.normalY, v2.normalZ) * bary.z;

    float3 N = normalize(mul((float3x3)ObjectToWorld3x4(), normalOS));
    float3 T, B;
    BuildONB(N, T, B);
    float3x3 TBN = float3x3(T, B, N);

    float3 V      = normalize(-WorldRayDirection());
    float3 hitPos = HitWorldPosition();
    
    RngState rng = InitRng(DispatchRaysIndex().xy, g_FrameIndex, 0u);
    
    // Simple Lambertian Material (uniform across all meshes)
    const float3 albedo = float3(g_TestAlbedo, g_TestAlbedo, g_TestAlbedo);

    uint N_SAMPLES = max(g_NumSamples, 1u);

    // L_o ≈ (1/N) · Σᵢ [ f_r · L_i(ωᵢ) · cosθᵢ ] / p(ωᵢ)
    float3 brdf  = albedo / PI;
    float3 accum = 0.0;
    for (uint i = 0; i < N_SAMPLES; ++i)
    {
        float2 u = NextFloat2(rng);

        float3 dirT;
        float  pdf;
        SampleHemisphere_Uniform(u, dirT, pdf);

        float3 dirW     = mul(dirT, TBN);
        float  cosTheta = dirT.z;
        if (cosTheta <= 0)
            continue;

        float3 Li = SampleSkyRadiance(OffsetRay(hitPos, N), dirW);

        accum += brdf * Li * cosTheta / pdf;
    }

    payload.radiance = accum / float(N_SAMPLES);
}
