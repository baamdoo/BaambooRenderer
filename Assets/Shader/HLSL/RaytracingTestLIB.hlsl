#define _CAMERA
#define _MESH
#define _MATERIAL
#include "BxDf.hlsli"
#include "HelperFunctions.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_Output : register(b0, ROOT_CONSTANT_SPACE);

RaytracingAccelerationStructure g_Scene : register(t0, space1);


// ───────────────────────────────────────────────────────────────────
// Types
// ───────────────────────────────────────────────────────────────────
struct Ray
{
    float3 origin;
    float3 direction;
};

struct RadiancePayload
{
    float3 radiance;
    float  depth;
    uint   rayRecursionDepth;
};


// ───────────────────────────────────────────────────────────────────
// Helper Functions
// ───────────────────────────────────────────────────────────────────
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

RadiancePayload TraceRadianceRay(in Ray ray, in uint curRayRecursionDepth, in uint maxRecursionDepth, float tMin, float tMax, bool bCullNonOpaque, bool bCullBackFace)
{
    RadiancePayload payload = (RadiancePayload)0;

    payload.radiance = 0;
    if (curRayRecursionDepth >= maxRecursionDepth)
    {
        payload.radiance = float3(1, 1, 1);
        return payload;
    }
    payload.rayRecursionDepth = curRayRecursionDepth + 1;

    RayDesc rayDesc;
    rayDesc.Origin    = ray.origin;
    rayDesc.Direction = ray.direction;
    rayDesc.TMin      = tMin;
    rayDesc.TMax      = tMax;

    uint rayFlags = 0;
    if (bCullNonOpaque)
    {
        rayFlags |= RAY_FLAG_CULL_NON_OPAQUE;
    }
    if (bCullBackFace)
    {
        rayFlags |= RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
    }
    // TraceRay(g_Scene, rayFlags, ~0, 0, 2, 0, rayDesc, rayPayload);    // radiance
    // TraceRay(g_Scene, rayFlags, ~0, 1, 2, 1, rayDesc, shadowPayload); // shadow
    TraceRay(g_Scene, rayFlags, ~0, 0, 1, 0, rayDesc, payload);

    return payload;
}

// ───────────────────────────────────────────────────────────────────
// Ray Generation
// ───────────────────────────────────────────────────────────────────
[shader("raygeneration")]
void RayGen()
{
    RWTexture2D< float4 > Output = GetResource(g_Output.index);

    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim   = DispatchRaysDimensions().xy;

    float2 uv = (float2(launchIndex) + 0.5) / float2(launchDim);

    float3 target    = ReconstructWorldPos(uv, 0.0, g_Camera.mViewProjInv); // reversed-Z
    float3 rayDir    = normalize(target.xyz - g_Camera.posWORLD);
    float3 rayOrigin = g_Camera.posWORLD;

    RayDesc ray;
    ray.Origin    = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin      = g_Camera.zNear;
    ray.TMax      = g_Camera.zFar;

    RadiancePayload payload = { float3(0.0, 0.0, 0.0), 0.0, 0 };

    TraceRay(
        g_Scene,
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
        0xFF,
        0,
        1,
        0,
        ray,
        payload
    );

    Output[launchIndex] = float4(payload.radiance, 1.0);
}


// ───────────────────────────────────────────────────────────────────
// Miss
// ───────────────────────────────────────────────────────────────────
[shader("miss")]
void Miss(inout RadiancePayload payload)
{
    payload.radiance = float3(0.9, 0.3, 0.4);
}


// ───────────────────────────────────────────────────────────────────
// Closest Hit
// ───────────────────────────────────────────────────────────────────
[shader("closesthit")]
void ClosestHit(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    StructuredBuffer< InstanceData > InstanceBuffer = GetResource(g_Instances.index);
    uint instanceID = InstanceID();
    InstanceData instance = InstanceBuffer[instanceID];

    uint primitiveIndex = PrimitiveIndex();
    uint indexOffset    = instance.iOffset + (primitiveIndex * 3);

    StructuredBuffer< uint > IndexBuffer = GetResource(g_Indices.index);
    uint i0 = IndexBuffer[indexOffset + 0];
    uint i1 = IndexBuffer[indexOffset + 1];
    uint i2 = IndexBuffer[indexOffset + 2];

    StructuredBuffer< Vertex > VertexBuffer = GetResource(g_Vertices.index);
    Vertex v0 = VertexBuffer[instance.vOffset + i0];
    Vertex v1 = VertexBuffer[instance.vOffset + i1];
    Vertex v2 = VertexBuffer[instance.vOffset + i2];

    StructuredBuffer< MaterialData > MaterialBuffer = GetResource(g_Materials.index);
    MaterialData material = MaterialBuffer[instance.materialID];

    float3 barycentrics = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv     = float2(v0.u, v0.v) * barycentrics.x + float2(v1.u, v1.v) * barycentrics.y + float2(v2.u, v2.v) * barycentrics.z;
    float3 normal = 
        float3(v0.normalX, v0.normalY, v0.normalZ) * barycentrics.x + 
        float3(v1.normalX, v1.normalY, v1.normalZ) * barycentrics.y +
        float3(v2.normalX, v2.normalY, v2.normalZ) * barycentrics.z;

    float3 albedo = float3(material.tintR, material.tintG, material.tintB);
    if (material.albedoID != INVALID_INDEX)
    {
        Texture2D albedoTex = GetResource(material.albedoID);

        albedo *= albedoTex.SampleLevel(g_LinearWrapSampler, uv, 0).rgb;
    }

    float3 hitPosition = HitWorldPosition();

    payload.depth    = hitPosition.z;
    payload.radiance = albedo;
}
