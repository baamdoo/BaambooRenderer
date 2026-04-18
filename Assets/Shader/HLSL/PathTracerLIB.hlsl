#define _CAMERA
#define _MESH
#include "Common.hlsli"
#include "Sampling.hlsli"


// ───────────────────────────────────────────────────────────────────
// Bindings
// ───────────────────────────────────────────────────────────────────
cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint  g_FrameIndex;        // monotonically increasing frame counter (RNG seed)
    uint  g_AccumReset;        // 1 = overwrite accum with current sample; 0 = add
    uint  g_NumSamples;        // paths per pixel this frame (Phase 1 baseline: 1)
    uint  g_MaxDepth;          // path length cap (Phase 1 Step 1.4)
    uint  g_EnableRR;          // 1 = Russian Roulette on (Step 1.9)
    uint  g_RRMinDepth;        // bounces before RR may fire (Step 1.9)
    uint  g_FurnaceMode;       // 1 = ignore skybox, use constant L_env; 0 = real sky
    float g_FurnaceLenvR;      // channel-specific constant sky radiance (furnace)
    float g_FurnaceLenvG;
    float g_FurnaceLenvB;
    float g_TestAlbedo;        // Phase 1 constant Lambertian reflectance (ρ)
};

ConstantBuffer< DescriptorHeapIndex > g_Skybox      : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_AccumBuffer : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Display     : register(b3, ROOT_CONSTANT_SPACE);

RaytracingAccelerationStructure g_Scene : register(t0, space1);


// ───────────────────────────────────────────────────────────────────
// Payload
// ───────────────────────────────────────────────────────────────────
struct HitPayload
{
    float3 position;      // world-space hit point (undefined on miss)
    uint   hitKind;       // 0 = miss, 1 = surface hit
    float3 normal;        // shading normal (world space)
    float  _pad0;
    float3 albedo;        // Lambertian ρ at this hit (Phase 1 placeholder)
    float  _pad1;
    float3 emission;      // surface L_e (0 throughout Phase 1)
    float  _pad2;
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
// Ray Generation — iterative random walk
// ───────────────────────────────────────────────────────────────────
[shader("raygeneration")]
void RayGen()
{
    RWTexture2D< float4 > AccumBuf = GetResource(g_AccumBuffer.index);
    RWTexture2D< float4 > Display  = GetResource(g_Display.index);

    uint2 rayIndex      = DispatchRaysIndex().xy;
    uint2 rayDimensions = DispatchRaysDimensions().xy;

    // NDC → world primary ray. Reverse-Z: near plane is at clip depth 1.
    float2 uv            = (float2(rayIndex) + 0.5) / float2(rayDimensions);
    float3 target        = ReconstructWorldPos(uv, 0.0, g_Camera.mViewProjInv);
    float3 primaryDir    = normalize(target - g_Camera.posWORLD);
    float3 primaryOrigin = g_Camera.posWORLD;

    uint N_PATHS = max(g_NumSamples, 1u);
    uint D_MAX   = max(g_MaxDepth,   1u);

    float3 Lsum = float3(0, 0, 0);
    for (uint pathIdx = 0; pathIdx < N_PATHS; ++pathIdx)
    {
        // Fresh RNG per path: (pixel, frameIndex, pathIdx) triple makes every path in the whole render independent.
        RngState rng = InitRng(rayIndex, g_FrameIndex, pathIdx);

        float3 β = float3(1, 1, 1);
        float3 L = float3(0, 0, 0);
        
        RayDesc ray;
        ray.Origin    = primaryOrigin;
        ray.Direction = primaryDir;
        ray.TMin      = g_Camera.zNear;
        ray.TMax      = g_Camera.zFar;

        for (uint depth = 0; depth < D_MAX; ++depth)
        {
            HitPayload hp = (HitPayload)0;
            TraceRay(
                g_Scene,
                RAY_FLAG_NONE, 
                0xFF,
                0,
                1,
                0,
                ray,
                hp);
            
            if (hp.hitKind == 0)
            {
                L += β * EvaluateSkyRadiance(ray.Direction);
                break;
            }
            
            L += β * hp.emission;
            
            float3 T, B;
            BuildONB(hp.normal, T, B);
            float3x3 TBN = float3x3(T, B, hp.normal);

            float2 u = NextFloat2(rng);

            float3 dirT;
            float  pdf;
            SampleHemisphere_Uniform(u, dirT, pdf);

            float3 dirW = mul(dirT, TBN);

            float3 brdf     = hp.albedo / PI;
            float  cosTheta = dot(dirW, hp.normal);
            if (cosTheta <= 0.0)
            {
                β = 0.0;
                break;
            }

            β *= brdf * cosTheta / pdf;

            ray.Origin    = OffsetRay(hp.position, hp.normal);
            ray.Direction = dirW;
            ray.TMin      = 0.0;
            ray.TMax      = g_Camera.zFar;

            // ===================== Russian-Roulette =====================
            if (g_EnableRR != 0u && depth >= g_RRMinDepth)
            {
                float qSurvive = clamp(max(β.r, max(β.g, β.b)), 0.0, 0.95);
                float q  = 1.0 - qSurvive;
                float xi = NextFloat(rng);
                if (xi < q)
                    break;
                
                β /= qSurvive;
            }
            // =============================================================
        }

        if (any(isnan(L)) || any(isinf(L)))
            L = float3(0, 0, 0);

        Lsum += L;
    }
    
    float3 Lframe = Lsum / float(N_PATHS);

    float4 prev = AccumBuf[rayIndex];
    float3 newSum;
    float  newCnt;

    if (g_AccumReset != 0)
    {
        newSum = Lframe;
        newCnt = 1.0;
    }
    else
    {
        newSum = prev.xyz + Lframe;
        newCnt = prev.w + 1.0;
    }

    AccumBuf[rayIndex] = float4(newSum, newCnt);
    Display[rayIndex]  = float4(newSum / max(newCnt, 1.0), 1.0);
}


// ───────────────────────────────────────────────────────────────────
// Primary Miss — the ray hit nothing. Just flag it for RayGen; RayGen
// itself reads ray.Direction and calls EvaluateSkyRadiance.
// ───────────────────────────────────────────────────────────────────
[shader("miss")]
void PrimaryMiss(inout HitPayload hp)
{
    hp.hitKind = 0u;
    // Leaving other fields untouched — RayGen doesn't read them on miss.
}


// ───────────────────────────────────────────────────────────────────
// Primary Closest Hit — report only surface info to RayGen
// ───────────────────────────────────────────────────────────────────
[shader("closesthit")]
void ClosestHit_Primary(inout HitPayload hp, in BuiltInTriangleIntersectionAttributes attr)
{
    // --- Vertex unpacking (unchanged from Phase 1 original) ----------
    StructuredBuffer< MeshData >     Meshes      = GetResource(g_Meshes.index);
    StructuredBuffer< InstanceData > Instances   = GetResource(g_Instances.index);
    StructuredBuffer< uint >         IndexBuffer = GetResource(g_Indices.index);
    StructuredBuffer< Vertex >       VertexBuf   = GetResource(g_Vertices.index);

    InstanceData instance = Instances[InstanceID()];
    MeshData     mesh     = Meshes[instance.meshID];

    uint primIdx   = PrimitiveIndex();
    uint indexBase = mesh.lods[0].iOffset + primIdx * 3;
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

    float3 nWorld = normalize(mul((float3x3)ObjectToWorld3x4(), normalOS));

    // Double-sided Lambertian: force the shading normal to face the incoming ray.
    if (dot(nWorld, WorldRayDirection()) > 0.0)
        nWorld = -nWorld;

    hp.hitKind  = 1u;
    hp.position = HitWorldPosition();
    hp.normal   = nWorld;
    hp.albedo   = float3(g_TestAlbedo, g_TestAlbedo, g_TestAlbedo);   // Phase 1 placeholder
    hp.emission = float3(0, 0, 0);                                    // real emission in Phase 5+
}
