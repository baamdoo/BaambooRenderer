#ifndef _HLSL_CULLING_COMMON_HEADER
#define _HLSL_CULLING_COMMON_HEADER


// =========================================================================
// Frustum Culling
// =========================================================================
bool IsFrustumCulled(float4 planes[6], float3 centerWS, float radius)
{
    [unroll]
    for (int i = 0; i < 5; ++i)
        if (dot(planes[i], float4(centerWS, 1.0)) + radius < 0.0)
            return true;
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
// Occlusion Culling : 
//     Sphere-vs-HiZ occlusion test for reversed-Z depth (0=far, 1=near).
//
// Uses Mara/McGuire 2013 tight AABB projection for correct screen-space
// bounds regardless of aspect ratio. The HiZ sampler must use MIN
// reduction mode so that a single textureLod returns the conservative
// minimum depth within the sampled region.
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
    // --- 1. Transform to view space (positive Z = forward) ---
    float3 c = mul(view, float4(centerWS, 1.0)).xyz;

    // Behind or intersecting near plane: conservatively visible
    if (c.z < radius + zNear)
        return false;

    // --- 2. Mara/McGuire 2013: tight AABB of projected sphere ---
    float3 cr   = c * radius;
    float  czr2 = c.z * c.z - radius * radius;

    float vx   = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    float vy   = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    // --- 3. Project to UV [0,1] (DX12: flip Y) ---
    float4 aabb = float4(minx * P00, miny * P11, maxx * P00, maxy * P11);
    aabb.xz = aabb.xz *  0.5 + 0.5; // X: NDC -> UV
    aabb.yw = aabb.yw * -0.5 + 0.5; // Y: NDC -> UV (DX12 Y flip)

    // Ensure correct min/max ordering
    float2 bMin = min(aabb.xy, aabb.zw);
    float2 bMax = max(aabb.xy, aabb.zw);
    bMin = clamp(bMin, 0.0, 1.0);
    bMax = clamp(bMax, 0.0, 1.0);

    // --- 4. Select HiZ mip level ---
    // LINEAR + MIN reduction sampler → one sample covers 2x2 texels = 2^(L+1) px.
    float width  = (bMax.x - bMin.x) * float(pyramidWidth);
    float height = (bMax.y - bMin.y) * float(pyramidHeight);
    float level  = floor(log2(max(max(width, height), 1.0)));

    // --- 5. Sample HiZ (MIN reduction sampler -> single sample is conservative) ---
    float depth = hiZ.SampleLevel(PointClampMinSampler, (bMin + bMax) * 0.5, level);

    // --- 6. Depth test (reversed-Z: near=1, far=0) ---
    float depthSphere = zNear / (c.z - radius);

    // Occluded if sphere's nearest depth < HiZ minimum (farther in reversed-Z)
    return depthSphere < depth;
}

#endif // _HLSL_CULLING_COMMON_HEADER
