#define _CAMERA
#define _MESH
#define _MATERIAL
#define _LIGHT
#include "Common.hlsli"
#include "Sampling.hlsli"


// ───────────────────────────────────────────────────────────────────
// Ray type indexing (Phase 3 NEE)
// ───────────────────────────────────────────────────────────────────
// SBT layout: hit groups and miss shaders are added in this order, so
// the indices below also serve as the SBT record indices the engine
// hands out. If a future phase inserts a new ray type, bump these
// constants in lockstep with PathTracerNode's PSO/SBT build.
#define RAY_TYPE_PRIMARY 0
#define RAY_TYPE_SHADOW  1
#define NUM_RAY_TYPES    2


// ───────────────────────────────────────────────────────────────────
// Bindings
// ───────────────────────────────────────────────────────────────────
cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_FrameIndex; // monotonically increasing frame counter (RNG seed)
    uint g_AccumReset; // 1 = overwrite accum with current sample; 0 = add
    uint g_NumSamples; // paths per pixel this frame
};

ConstantBuffer< DescriptorHeapIndex > g_Skybox      : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_AccumBuffer : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Display     : register(b3, ROOT_CONSTANT_SPACE);

struct PathTracerSettings
{
    uint  maxDepth;          // path length cap
    uint  enableRR;          // 1 = Russian Roulette on
    uint  rrMinDepth;        // bounces before RR may fire
    uint  samplingStrategy;  // 0 = uniform hemisphere, 1 = cosine-weighted

    uint  furnaceMode;       // 1 = ignore skybox, use constant L_env; 0 = real sky
    float testAlbedo;        // constant Lambertian reflectance (rho) for furnace tests
    uint  neeEnable;         // 1 = perform NEE per bounce, 0 = BSDF only
    uint  _pad0;

    float furnaceLenvR;      // constant sky radiance (furnace mode)
    float furnaceLenvG;
    float furnaceLenvB;
    float _pad1;

    // Phase 4 MIS controls (Step 4.3).
    uint  misEnable;         // 1 = MIS on, 0 = Phase 3 NEE-only behavior
    uint  misHeuristic;      // 0 = balance, 1 = power(beta=2)
    uint  misForceWeight;    // 0 = heuristic, 1 = w_NEE=1, 2 = w_NEE=0, 3 = w_NEE=0.5
    uint  _pad2;
};
ConstantBuffer< PathTracerSettings > g_Settings : register(b0, space1);

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

struct ShadowPayload
{
    uint visible;
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
    if (g_Settings.furnaceMode != 0)
    {
        return float3(g_Settings.furnaceLenvR, g_Settings.furnaceLenvG, g_Settings.furnaceLenvB);
    }

    TextureCube< float3 > Skybox = GetResource(g_Skybox.index);
    return Skybox.SampleLevel(g_LinearClampSampler, dir, 0);
}

int IntersectClosestAnalyticLight(float3 origin, float3 dir, float tMax,
                                  out float tHit, out float3 normalAtHit,
                                  out float3 emissionAtHit)
{
    int    bestIdx = -1;
    float  bestT   = tMax;
    float3 bestN   = float3(0, 0, 0);
    float3 bestE   = float3(0, 0, 0);

    [loop] for (uint i = 0; i < g_Lights.numAreas; ++i)
    {
        float t; float3 n;
        if (IntersectRayAreaLight(origin, dir, g_Lights.areas[i], t, n) && t < bestT)
        {
            bestT   = t;
            bestN   = n;
            bestIdx = (int)i;
            bestE   = float3(g_Lights.areas[i].colorR,
                             g_Lights.areas[i].colorG,
                             g_Lights.areas[i].colorB) * g_Lights.areas[i].luminousFluxLm;
        }
    }

    [loop] for (uint j = 0; j < g_Lights.numSpheres; ++j)
    {
        float t; float3 n;
        if (IntersectRaySphereLight(origin, dir, g_Lights.spheres[j], t, n) && t < bestT)
        {
            bestT   = t;
            bestN   = n;
            bestIdx = (int)(g_Lights.numAreas + j);
            bestE   = float3(g_Lights.spheres[j].colorR,
                             g_Lights.spheres[j].colorG,
                             g_Lights.spheres[j].colorB) * g_Lights.spheres[j].luminousFluxLm;
        }
    }

    tHit          = bestT;
    normalAtHit   = bestN;
    emissionAtHit = bestE;
    return bestIdx;
}

float LightAreaPdf(int lightIdx)
{
    if (lightIdx < (int)g_Lights.numAreas)
    {
        AreaLight L = g_Lights.areas[lightIdx];
        return 1.0 / (4.0 * L.halfWidth * L.halfHeight);
    }
    else
    {
        SphereLight L = g_Lights.spheres[lightIdx - g_Lights.numAreas];
        return 1.0 / (4.0 * PI * L.radius * L.radius);
    }
}


// ───────────────────────────────────────────────────────────────────
// Shadow ray helper (Phase 3 NEE)
// ───────────────────────────────────────────────────────────────────
uint TraceShadowRay(float3 origin, float3 dir, float tMax)
{
    ShadowPayload sp;
    sp.visible = 0u;

    RayDesc r;
    r.Origin    = origin;
    r.Direction = dir;
    r.TMin      = 0.001;
    r.TMax      = tMax;

    TraceRay(
        g_Scene,
        RAY_FLAG_FORCE_NON_OPAQUE
            | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
            | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF,
        RAY_TYPE_SHADOW,    // RayContributionToHitGroupIndex
        NUM_RAY_TYPES,      // MultiplierForGeometryContributionToHitGroupIndex
        1,                  // MissShaderIndex (ShadowMiss)
        r,
        sp);

    return sp.visible;
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

    uint N_PATHS = max(g_NumSamples,        1u);
    uint D_MAX   = max(g_Settings.maxDepth, 1u);

    float3 Lsum = float3(0, 0, 0);
    for (uint pathIdx = 0; pathIdx < N_PATHS; ++pathIdx)
    {
        // Fresh RNG per path: (pixel, frameIndex, pathIdx) triple makes every path in the whole render independent.
        RngState rng = InitRng(rayIndex, g_FrameIndex, pathIdx);

        float3 β = float3(1, 1, 1);
        float3 L = float3(0, 0, 0);
        
        float prevPdfBSDF = 0.0;

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
                RAY_TYPE_PRIMARY,    // RayContributionToHitGroupIndex
                NUM_RAY_TYPES,       // MultiplierForGeometryContributionToHitGroupIndex
                0,                   // MissShaderIndex (PrimaryMiss)
                ray,
                hp);
            
            // ─────────────────────────────────────────────────────────────
            // Analytic light intersection
            // ─────────────────────────────────────────────────────────────
            int    lightIdx = -1;
            float  lightT;
            float3 lightN;
            float3 lightE;
            if (depth == 0u || g_Settings.misEnable != 0u)
            {
                float geomT = (hp.hitKind == 1u)
                              ? length(hp.position - ray.Origin)
                              : FLT_MAX;
                lightIdx = IntersectClosestAnalyticLight(
                              ray.Origin, ray.Direction, geomT,
                              lightT, lightN, lightE);
            }

            // Case A: BSDF random walk hit a light.
            if (lightIdx >= 0)
            {
                if (depth == 0u)
                {
                    // Camera sees a light directly. NEE has not yet fired at primary ray.
                    L += β * lightE;
                }
                else if (g_Settings.misEnable != 0u)
                {
                    float r       = lightT;
                    float cosY    = dot(lightN, -ray.Direction);
                    float pdfA    = LightAreaPdf(lightIdx);
                    float pickPdf = 1.0 / float(g_Lights.numAreas + g_Lights.numSpheres);
                    
                    float pdfNEE  = pickPdf * pdfA * r * r / cosY; // solid-angle pdf of NEE
                    float pdfBSDF = prevPdfBSDF;
                    
                    float wNEE;
                    if (g_Settings.misEnable == 0u)
                        wNEE = 1.0;
                    else if (g_Settings.misForceWeight == 1u)
                        wNEE = 1.0;
                    else if (g_Settings.misForceWeight == 2u)
                        wNEE = 0.0;
                    else if (g_Settings.misForceWeight == 3u)
                        wNEE = 0.5;
                    else
                        wNEE = g_Settings.misHeuristic == 1 ?
                                pdfNEE * pdfNEE / (pdfNEE * pdfNEE + pdfBSDF * pdfBSDF) :
                                pdfNEE / (pdfNEE + pdfBSDF);
                    
                    float wBSDF = 1.0 - wNEE;
                        
                    L += β * wBSDF * lightE;
                }
                
                break; // terminate path if bsdf ray hits the light source
            }

            // Case B: ray missed every geometry / light -> sky.
            if (hp.hitKind == 0u)
            {
                L += β * EvaluateSkyRadiance(ray.Direction);
                break;
            }

            // Case C: ordinary surface hit.
            L += β * hp.emission;

            // ===================== NEE ======================
            if (g_Settings.neeEnable != 0u)
            {
                uint numAreas   = g_Lights.numAreas;
                uint numSpheres = g_Lights.numSpheres;
                uint numLights  = numAreas + numSpheres;

                if (numLights > 0u)
                {
                    float pickXi  = NextFloat(rng);
                    float pickPdf = 1.0 / (float)numLights;

                    float3 y;
                    float3 normalY;
                    float  pdfA;
                    float3 emission;
                    uint  lightIdx = min((uint)(pickXi * (float)numLights), numLights - 1u);
                    if (lightIdx < numAreas)
                    {
                        AreaLight light = g_Lights.areas[lightIdx];
                        
                        float2 uLight = NextFloat2(rng);
                        SampleAreaLight(uLight, light, y, normalY, pdfA);
                        emission = float3(light.colorR, light.colorG, light.colorB) * light.luminousFluxLm;
                    }
                    else
                    {
                        SphereLight light = g_Lights.spheres[lightIdx - numAreas];
                        
                        float2 uLight = NextFloat2(rng);
                        SampleSphereLight(uLight, light, y, normalY, pdfA);
                        emission = float3(light.colorR, light.colorG, light.colorB) * light.luminousFluxLm;
                    }
                    pdfA *= pickPdf; // Consider the probability choosing a certain light

                    float3 toLight = y - hp.position;
                    float  r       = length(toLight);
                    float3 dir     = toLight / r;

                    float cosX = dot(hp.normal, dir);
                    float cosY = dot(normalY, -dir);
                    if (cosX > 0.0 && cosY > 0.0)
                    {
                        float pdfNEE = pdfA * r * r / cosY;     // solid-angle pdf of NEE
                        uint  visible  = TraceShadowRay(OffsetRay(hp.position, hp.normal), dir, r - 0.001);
                        
                        float pdfBSDF = EvaluateBSDFPdf(dir, hp.normal, g_Settings.samplingStrategy);
                        float wNEE;
                        if (g_Settings.misEnable == 0u)
                            wNEE = 1.0;
                        else if (g_Settings.misForceWeight == 1u)
                            wNEE = 1.0;
                        else if (g_Settings.misForceWeight == 2u)
                            wNEE = 0.0;
                        else if (g_Settings.misForceWeight == 3u)
                            wNEE = 0.5;
                        else
                            wNEE = g_Settings.misHeuristic == 1 ?
                                pdfNEE * pdfNEE / (pdfNEE * pdfNEE + pdfBSDF * pdfBSDF) :
                                pdfNEE / (pdfNEE + pdfBSDF);
                        
                        L += β * wNEE * (hp.albedo / PI) * cosX * emission * (float)visible / pdfNEE;
                    }
                }
            }
            // ============================================================================

            float3 T, B;
            BuildONB(hp.normal, T, B);
            float3x3 TBN = float3x3(T, B, hp.normal);

            float2 u = NextFloat2(rng);

            float3 dirT;
            float  pdf;
            if (g_Settings.samplingStrategy == 1u)
                SampleHemisphere_Cosine(u, dirT, pdf);
            else
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
            prevPdfBSDF = pdf;

            ray.Origin    = OffsetRay(hp.position, hp.normal);
            ray.Direction = dirW;
            ray.TMin      = 0.0;
            ray.TMax      = g_Camera.zFar;

            // ===================== Russian-Roulette =====================
            if (g_Settings.enableRR != 0u && depth >= g_Settings.rrMinDepth)
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
// Shadow Miss / AnyHit
// ───────────────────────────────────────────────────────────────────
[shader("miss")]
void ShadowMiss(inout ShadowPayload sp)
{
    sp.visible = 1u;
}

[shader("anyhit")]
void AnyHit_Shadow(inout ShadowPayload sp, in BuiltInTriangleIntersectionAttributes attr)
{
    AcceptHitAndEndSearch();
}


// ───────────────────────────────────────────────────────────────────
// Primary Closest Hit — report only surface info to RayGen
// ───────────────────────────────────────────────────────────────────
[shader("closesthit")]
void ClosestHit_Primary(inout HitPayload hp, in BuiltInTriangleIntersectionAttributes attr)
{
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

    // --- Material emission ----------------------------------
    StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);
    MaterialData mat = Materials[instance.materialID];
    float3 emission  = float3(mat.tintR, mat.tintG, mat.tintB) * mat.emissivePower;

    hp.hitKind  = 1u;
    hp.position = HitWorldPosition();
    hp.normal   = nWorld;
    hp.albedo   = float3(g_Settings.testAlbedo, g_Settings.testAlbedo, g_Settings.testAlbedo);
    hp.emission = emission;
}
