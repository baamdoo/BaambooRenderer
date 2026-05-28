#ifndef _HLSL_CULLING_COMMON_HEADER
#define _HLSL_CULLING_COMMON_HEADER

// =========================================================================
// Frustum Culling
// =========================================================================
bool IsFrustumCulled(float4 planes[6], float3 centerWS, float radius)
{
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        if (dot(planes[i], float4(centerWS, 1.0)) + radius < 0.0)
            return true;
    }
    return false;
}

// =========================================================================
// Backface Culling
// =========================================================================
bool IsConeCulled(float4 cone, float3 sphereCenterWS, float sphereRadius, float3 viewPosWS)
{
    float3 toSphere = sphereCenterWS - viewPosWS;
    return dot(cone.xyz, toSphere) >= cone.w * length(toSphere) + sphereRadius;
}

// =========================================================================
// Sphere-vs-HiZ occlusion
// =========================================================================
bool IsOccluded(float3           centerWS,
                float            radius,
                float4x4         view,
                float            P00,
                float            P11,
                float            zNear,
                Texture2D<float> hiZ,
                SamplerState     PointClampMinSampler,
                uint             pyramidWidth,
                uint             pyramidHeight)
{
    float3 c = mul(view, float4(centerWS, 1.0)).xyz;

    if (c.z < radius + zNear)
        return false;

    float3 cr   = c * radius;
    float  czr2 = c.z * c.z - radius * radius;

    float vx   = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    float vy   = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    float4 aabb = float4(minx * P00, miny * P11, maxx * P00, maxy * P11);
    aabb.xz = aabb.xz *  0.5 + 0.5;
    aabb.yw = aabb.yw * -0.5 + 0.5;

    float2 bMin = min(aabb.xy, aabb.zw);
    float2 bMax = max(aabb.xy, aabb.zw);
    bMin = clamp(bMin, 0.0, 1.0);
    bMax = clamp(bMax, 0.0, 1.0);

    float width  = (bMax.x - bMin.x) * float(pyramidWidth);
    float height = (bMax.y - bMin.y) * float(pyramidHeight);
    float level  = floor(log2(max(max(width, height), 1.0)));

    float depth = hiZ.SampleLevel(PointClampMinSampler, (bMin + bMax) * 0.5, level);
    float depthSphere = zNear / (c.z - radius);

    return depthSphere < depth;
}

// =========================================================================
// AABB-vs-HiZ occlusion
// =========================================================================
bool IsAABBOccluded(float3           aabbMin,
                    float3           aabbMax,
                    float4x4         view,
                    float4x4         proj,
                    float            zNear,
                    Texture2D<float> hiZ,
                    SamplerState     LinearClampMinSampler,
                    uint             pyramidWidth,
                    uint             pyramidHeight)
{
    float2 ndcMin =  10000.0;
    float2 ndcMax = -10000.0;
    float  viewZMin = 1.0e30;

    [unroll]
    for (uint i = 0u; i < 8u; ++i)
    {
        const float3 cornerWS = float3(
            (i & 1u) ? aabbMax.x : aabbMin.x,
            (i & 2u) ? aabbMax.y : aabbMin.y,
            (i & 4u) ? aabbMax.z : aabbMin.z);

        const float4 cornerVS = mul(view, float4(cornerWS, 1.0));
        if (cornerVS.z < zNear)
            return false;

        viewZMin = min(viewZMin, cornerVS.z);

        const float4 cornerCS = mul(proj, cornerVS);
        const float2 cornerND = cornerCS.xy / cornerCS.w;
        ndcMin = min(ndcMin, cornerND);
        ndcMax = max(ndcMax, cornerND);
    }

    float2 uvMin = float2(ndcMin.x * 0.5 + 0.5, ndcMax.y * -0.5 + 0.5);
    float2 uvMax = float2(ndcMax.x * 0.5 + 0.5, ndcMin.y * -0.5 + 0.5);
    uvMin = clamp(uvMin, 0.0, 1.0);
    uvMax = clamp(uvMax, 0.0, 1.0);

    if (uvMin.x >= uvMax.x || uvMin.y >= uvMax.y)
        return false;

    const float widthPx  = (uvMax.x - uvMin.x) * float(pyramidWidth);
    const float heightPx = (uvMax.y - uvMin.y) * float(pyramidHeight);
    const float level    = floor(log2(max(max(widthPx, heightPx), 1.0)));

    const float2 uvCenter = (uvMin + uvMax) * 0.5;
    float hiZDepth = hiZ.SampleLevel(LinearClampMinSampler, uvCenter, level);
    hiZDepth = min(hiZDepth, hiZ.SampleLevel(LinearClampMinSampler, uvMin, level));
    hiZDepth = min(hiZDepth, hiZ.SampleLevel(LinearClampMinSampler, uvMax, level));
    hiZDepth = min(hiZDepth, hiZ.SampleLevel(LinearClampMinSampler, float2(uvMin.x, uvMax.y), level));
    hiZDepth = min(hiZDepth, hiZ.SampleLevel(LinearClampMinSampler, float2(uvMax.x, uvMin.y), level));

    const float depthAABB = zNear / viewZMin;
    return depthAABB < hiZDepth - 1.0e-5f;
}

#endif // _HLSL_CULLING_COMMON_HEADER
