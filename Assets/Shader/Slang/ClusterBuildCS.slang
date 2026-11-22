#include "ShaderShared/Scene.h"
#include "ShaderShared/Light.h"
#include "Common/Binding.slangh"
#include "Common/LightCulling.slangh"
#include "Common/Math.slangh"

struct ClusterBuildPush
{
    ClusterGridConstants constants;

    uint ID_Scene;
    uint ID_ClusterBuffer;
};

[[vk::push_constant]] ConstantBuffer< ClusterBuildPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// The real viewport, not the tile-rounded grid size: lighting maps pixels to clusters the same way
float2 PixelToNDC(float2 p, float2 viewport)
{
    return float2(2.0 * (p.x / viewport.x) - 1.0, 1.0 - 2.0 * (p.y / viewport.y));
}

[numthreads(4, 4, 4)]
void main(uint3 tID : SV_DispatchThreadID)
{
    if (tID.x >= g_Push.constants.numTilesX || tID.y >= g_Push.constants.numTilesY || tID.z >= CLUSTER_SLICES_Z)
        return;

    StructuredBuffer< SceneGlobals > scene = GetResource(g_Push.ID_Scene);

    const float2 viewport = scene[0].frozenCamera.viewport;
    const float  zNear    = scene[0].frozenCamera.zNear;
    const float  zFar     = scene[0].frozenCamera.zFar;
    const float  P00      = scene[0].frozenCamera.mProj[0][0];
    const float  P11      = scene[0].frozenCamera.mProj[1][1];

    const float2 tileMin = float2(tID.xy) * float(CLUSTER_TILE_SIZE_PX);
    const float2 tileMax = tileMin + float(CLUSTER_TILE_SIZE_PX);

    const float2 ndcLT = PixelToNDC(float2(tileMin.x, tileMin.y), viewport);
    const float2 ndcLB = PixelToNDC(float2(tileMin.x, tileMax.y), viewport);
    const float2 ndcRB = PixelToNDC(float2(tileMax.x, tileMax.y), viewport);
    const float2 ndcRT = PixelToNDC(float2(tileMax.x, tileMin.y), viewport);

    // log(z) = lerp(log(zNear), log(zFar), k/Nz) => z = zNear * pow(zFar/zNear, k/Nz)
    const float zSliceNear = zNear * pow(zFar / zNear, float(tID.z) / float(CLUSTER_SLICES_Z));
    const float zSliceFar  = zNear * pow(zFar / zNear, float(tID.z + 1u) / float(CLUSTER_SLICES_Z));

    float3 cornersVS[8];
    cornersVS[0] = float3(ndcLT.x * zSliceNear / P00, ndcLT.y * zSliceNear / P11, zSliceNear); // near left-top
    cornersVS[1] = float3(ndcLB.x * zSliceNear / P00, ndcLB.y * zSliceNear / P11, zSliceNear); // near left-bottom
    cornersVS[2] = float3(ndcRB.x * zSliceNear / P00, ndcRB.y * zSliceNear / P11, zSliceNear); // near right-bottom
    cornersVS[3] = float3(ndcRT.x * zSliceNear / P00, ndcRT.y * zSliceNear / P11, zSliceNear); // near right-top
    cornersVS[4] = float3(ndcLT.x * zSliceFar / P00, ndcLT.y * zSliceFar / P11, zSliceFar);    // far left-top
    cornersVS[5] = float3(ndcLB.x * zSliceFar / P00, ndcLB.y * zSliceFar / P11, zSliceFar);    // far left-bottom
    cornersVS[6] = float3(ndcRB.x * zSliceFar / P00, ndcRB.y * zSliceFar / P11, zSliceFar);    // far right-bottom
    cornersVS[7] = float3(ndcRT.x * zSliceFar / P00, ndcRT.y * zSliceFar / P11, zSliceFar);    // far right-top

    float3 aabbMin = float3(FLT_MAX);
    float3 aabbMax = float3(-FLT_MAX);
    [unroll]
    for (uint i = 0; i < 8u; ++i)
    {
        aabbMin = min(aabbMin, cornersVS[i]);
        aabbMax = max(aabbMax, cornersVS[i]);
    }

    ClusterAABB aabb;
    aabb.aabbMin = float4(aabbMin, 0.0);
    aabb.aabbMax = float4(aabbMax, 0.0);

    RWStructuredBuffer< ClusterAABB > clusters = GetResource(g_Push.ID_ClusterBuffer);
    clusters[ClusterFlatIndex(tID, g_Push.constants.numTilesX, g_Push.constants.numTilesY)] = aabb;
}
