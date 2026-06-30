#ifndef SURFACE_RESOLVE_HLSLI
#define SURFACE_RESOLVE_HLSLI

#include "HelperFunctions.hlsli"
#include "VisibilityBuffer.hlsli"

#define MATCLASS_STANDARD 0u
#define MATCLASS_TERRAIN  1u


float2 SignNotZero(float2 v)
{
    return float2(v.x >= 0.0 ? 1.0 : -1.0,
                  v.y >= 0.0 ? 1.0 : -1.0);
}

float2 OctEncode(float3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    if (n.z < 0.0)
    {
        float2 oldXY = n.xy;
        n.xy = (1.0 - abs(oldXY.yx)) * SignNotZero(oldXY);
    }

    return n.xy;
}
float3 OctDecode(float2 e)
{
    float3 n = float3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));

    float t = saturate(-n.z);
    n.xy -= t * SignNotZero(n.xy);

    return normalize(n);
}

float3 Barycentrics(float2 ndc, float4 c0, float4 c1, float4 c2)
{
    float2 ndc0 = c0.xy / c0.w;
    float2 ndc1 = c1.xy / c1.w;
    float2 ndc2 = c2.xy / c2.w;

    float totalArea = EdgeFunction(ndc0, ndc1, ndc2);
    if (abs(totalArea) < 1e-6)
    {
        return float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0);
    }

    float a = EdgeFunction(ndc, ndc1, ndc2) / totalArea;
    float b = EdgeFunction(ndc, ndc2, ndc0) / totalArea;
    float c = 1.0 - a - b;

    float3 invZ = float3(1.0 / c0.w, 1.0 / c1.w, 1.0 / c2.w);
    float3 bary = float3(a, b, c) * invZ;
    return bary / dot(bary, float3(1.0, 1.0, 1.0)); // 1/Z = a/Z0 + b/Z1 + c/Z2
}

void UVGradient(float2 ndc, float2 invViewport, 
                float4 c0, float4 c1, float4 c2,
                float2 uv0, float2 uv1, float2 uv2,
                out float2 duvdx, out float2 duvdy)
{
    float3 bary   = Barycentrics(ndc, c0, c1, c2);
    float3 barydx = Barycentrics(ndc + float2(2.0 * invViewport.x, 0), c0, c1, c2);
    float3 barydy = Barycentrics(ndc + float2(0, -2.0 * invViewport.y), c0, c1, c2);

    float2 uv   = bary.x * uv0 + bary.y * uv1 + bary.z * uv2;
    float2 uvdx = barydx.x * uv0 + barydx.y * uv1 + barydx.z * uv2;
    float2 uvdy = barydy.x * uv0 + barydy.y * uv1 + barydy.z * uv2;

    duvdx = uvdx - uv;
    duvdy = uvdy - uv;
}


struct VisTriangle
{
    float3   positionLS[3];
    float2   uv[3];
    float3   normalLS[3];
    float3   tangentLS[3];

    float4x4 mLocalToWorld;
    float4x4 mWorldToLocal;

    uint     materialID;
};

VisTriangle FetchVisTriangle(uint v0, uint v1)
{
    StructuredBuffer< Vertex >        Vertices         = GetResource(g_MeshStreams.vertices);
    StructuredBuffer< Meshlet >       Meshlets         = GetResource(g_MeshStreams.meshlets);
    StructuredBuffer< uint >          MeshletVertices  = GetResource(g_MeshStreams.meshletVertices);
    StructuredBuffer< uint >          MeshletTriangles = GetResource(g_MeshStreams.meshletTriangles);
    StructuredBuffer< MeshData >      Meshes           = GetResource(g_Meshes.index);
    StructuredBuffer< InstanceData >  Instances        = GetResource(g_Instances.index);
    StructuredBuffer< TransformData > Transforms       = GetResource(g_Transforms.index);

    uint instanceID   = VisInstanceID(v0);
    uint lod          = VisLOD(v0);
    uint meshletIndex = VisMeshletIndex(v1);
    uint triLocal     = VisTriLocal(v1);

    InstanceData  inst = Instances[instanceID];
    MeshData      mesh = Meshes[inst.meshID];
    TransformData xf   = Transforms[inst.transformID];
    Meshlet       m    = Meshlets[meshletIndex];
    MeshLOD       ml   = mesh.lods[lod];

    uint tPacked3  = MeshletTriangles[ml.mtOffset + m.triangleOffset + triLocal];
    uint locals[3] = { tPacked3 & 0xFF, (tPacked3 >> 8) & 0xFF, (tPacked3 >> 16) & 0xFF };

    VisTriangle t;
    t.mLocalToWorld = xf.mLocalToWorld;
    t.mWorldToLocal = xf.mWorldToLocal;
    t.materialID    = inst.materialID;

    [unroll] for (uint k = 0; k < 3; ++k)
    {
        uint vi = mesh.vOffset + MeshletVertices[ml.mvOffset + m.vertexOffset + locals[k]];

        Vertex vv = Vertices[vi];
        t.positionLS[k] = float3(vv.posX, vv.posY, vv.posZ);
        t.uv[k]         = float2(vv.u, vv.v);
        t.normalLS[k]   = float3(vv.normalX, vv.normalY, vv.normalZ);
        t.tangentLS[k]  = float3(vv.tangentX, vv.tangentY, vv.tangentZ);
    }

    return t;
}

struct ResolvedSurface
{
    float3 N;         // world-space shading normal
    float  roughness; // linear
    uint   matClass;
    float3 baseColor;
    float  metallic;
};

// Full mesh-surface resolve: fetch -> perspective-correct bary -> interpolate ->
// analytic UV grad -> SampleGrad material. `pixelCenter` is (px + 0.5).
ResolvedSurface ResolveMeshSurface(uint v0, uint v1, float2 pixelCenter, float2 viewport)
{
    VisTriangle t = FetchVisTriangle(v0, v1);

    float4 c[3];
    [unroll] for (uint k = 0; k < 3; ++k)
    {
        float4 wp = mul(t.mLocalToWorld, float4(t.positionLS[k], 1.0));
        c[k] = mul(g_Camera.mViewProj, wp);
    }

    float2 ndc = (pixelCenter / viewport) * 2.0 - 1.0;
    ndc.y = -ndc.y; // pixel-space y is down; clip/NDC y is up

    float3 bary = Barycentrics(ndc, c[0], c[1], c[2]);

    float2 uv = bary.x * t.uv[0] + bary.y * t.uv[1] + bary.z * t.uv[2];
    float3 nL = bary.x * t.normalLS[0] + bary.y * t.normalLS[1] + bary.z * t.normalLS[2];
    float3 tL = bary.x * t.tangentLS[0] + bary.y * t.tangentLS[1] + bary.z * t.tangentLS[2];

    float3 N = normalize(mul(transpose((float3x3)t.mWorldToLocal), nL));
    float3 T = normalize(mul((float3x3)t.mLocalToWorld, tL));
    T = normalize(T - N * dot(T, N));

    float2 ddxUV, ddyUV;
    UVGradient(ndc, 1.0 / viewport, c[0], c[1], c[2], t.uv[0], t.uv[1], t.uv[2], ddxUV, ddyUV);

    ResolvedSurface rs;
    rs.matClass  = MATCLASS_STANDARD;
    rs.N         = N;
    rs.roughness = 1.0;
    rs.baseColor = float3(1.0, 1.0, 1.0);
    rs.metallic  = 0.0;

    if (t.materialID != INVALID_INDEX)
    {
        StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);
        MaterialData mat = Materials[t.materialID];

        float3 albedo = float3(mat.tintR, mat.tintG, mat.tintB);
        if (mat.albedoID != INVALID_INDEX)
        {
            Texture2D AlbedoMap = GetResource(mat.albedoID);
            albedo *= AlbedoMap.SampleGrad(g_LinearWrapSampler, uv, ddxUV, ddyUV).rgb;
        }

        float metallic  = mat.metallic;
        float roughness = mat.roughness;
        if (mat.metallicRoughnessAoID != INVALID_INDEX)
        {
            Texture2D OrmMap = GetResource(mat.metallicRoughnessAoID);
            float3 orm = OrmMap.SampleGrad(g_LinearWrapSampler, uv, ddxUV, ddyUV).rgb;
            metallic  *= orm.b;
            roughness *= orm.g;
        }

        // Tangent-space normal map (production path; skipped when the material has none).
        if (mat.normalID != INVALID_INDEX)
        {
            Texture2D NormalMap = GetResource(mat.normalID);

            float3 n = NormalMap.SampleGrad(g_LinearWrapSampler, uv, ddxUV, ddyUV).rgb * 2.0 - 1.0;

            float3 B = normalize(cross(N, T));
            float3x3 TBN = float3x3(T, B, N);

            N = normalize(mul(n, TBN));
        }

        rs.N         = N;
        rs.roughness = roughness;
        rs.baseColor = albedo;
        rs.metallic  = metallic;
    }

    return rs;
}

// DEBUG: Only the perspective-correct barycentrics for a VisID.
float3 DebugVisBary(uint v0, uint v1, float2 pixelCenter, float2 viewport)
{
    VisTriangle t = FetchVisTriangle(v0, v1);

    float4 c[3];
    [unroll] for (uint k = 0; k < 3; ++k)
        c[k] = mul(g_Camera.mViewProj, mul(t.mLocalToWorld, float4(t.positionLS[k], 1.0)));

    float2 ndc = (pixelCenter / viewport) * 2.0 - 1.0;
    ndc.y = -ndc.y;

    return Barycentrics(ndc, c[0], c[1], c[2]);
}

struct ResolvedMaterial
{
    float3 baseColor;
    float  metallic;
    float  ao;
    float3 emissive;
    uint   materialID;
};

ResolvedMaterial ResolveMaterial(uint v0, uint v1, float2 pixelCenter, float2 viewport)
{
    VisTriangle t = FetchVisTriangle(v0, v1);

    float4 c[3];
    [unroll] for (uint k = 0; k < 3; ++k)
    {
        float4 p = mul(t.mLocalToWorld, float4(t.positionLS[k], 1.0));
        c[k] = mul(g_Camera.mViewProj, p);
    }

    float2 ndc = (pixelCenter / viewport) * 2.0 - 1.0;
    ndc.y = -ndc.y;

    float3 bary = Barycentrics(ndc, c[0], c[1], c[2]);
    float2 uv   = bary.x * t.uv[0] + bary.y * t.uv[1] + bary.z * t.uv[2];

    float2 ddxUV, ddyUV;
    UVGradient(ndc, 1.0 / viewport, c[0], c[1], c[2], t.uv[0], t.uv[1], t.uv[2], ddxUV, ddyUV);

    ResolvedMaterial m;
    m.baseColor  = float3(0.0, 0.0, 0.0);
    m.metallic   = 0.0;
    m.ao         = 0.0;
    m.emissive   = float3(0.0, 0.0, 0.0);
    m.materialID = t.materialID;

    if (t.materialID != INVALID_INDEX)
    {
        StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);
        MaterialData mat = Materials[t.materialID];

        float3 albedo = float3(mat.tintR, mat.tintG, mat.tintB);
        if (mat.albedoID != INVALID_INDEX)
        {
            Texture2D AlbedoMap = GetResource(mat.albedoID);
            albedo *= AlbedoMap.SampleGrad(g_LinearWrapSampler, uv, ddxUV, ddyUV).rgb;
        }

        float metallic = mat.metallic;
        float ao       = 1.0;
        if (mat.metallicRoughnessAoID != INVALID_INDEX)
        {
            Texture2D OrmMap = GetResource(mat.metallicRoughnessAoID);

            float3 orm = OrmMap.SampleGrad(g_LinearWrapSampler, uv, ddxUV, ddyUV).rgb;
            metallic *= orm.b;
            ao       *= orm.r;
        }

        float3 emissive = float3(0.0, 0.0, 0.0);
        if (mat.emissiveID != INVALID_INDEX)
        {
            Texture2D EmissiveMap = GetResource(mat.emissiveID);
            emissive = EmissiveMap.SampleGrad(g_LinearWrapSampler, uv, ddxUV, ddyUV).rgb * mat.emissivePower;
        }

        m.baseColor = albedo;
        m.metallic  = metallic;
        m.ao        = ao;
        m.emissive  = emissive;
    }

    return m;
}

#endif // SURFACE_RESOLVE_HLSLI
