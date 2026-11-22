#include "ShaderShared/Scene.h"
#include "ShaderShared/Light.h"
#include "Common/Binding.slangh"
#include "Common/LightCulling.slangh"
#include "Common/Lights.slangh"

// Count, scan and write are three pipelines that share this push block
struct LightCullingPush
{
    ClusterGridConstants constants;

    uint ID_Scene;
    uint ID_ClusterBuffer;
    uint ID_LightGridBuffer;     // uint2(offset, count) per cluster
    uint ID_LightListDataBuffer; // EncodeLightIndex(type, idx)
};

[[vk::push_constant]] ConstantBuffer< LightCullingPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

bool IsInsideClusterGrid(uint3 cluster)
{
    return cluster.x < g_Push.constants.numTilesX && cluster.y < g_Push.constants.numTilesY && cluster.z < CLUSTER_SLICES_Z;
}

uint ClusterIndex(uint3 cluster)
{
    return ClusterFlatIndex(cluster, g_Push.constants.numTilesX, g_Push.constants.numTilesY);
}

// Count and write visit the light types in the same order, so the write pass keeps exactly the lights the count pass capped
[numthreads(4, 4, 4)]
void CountLights(uint3 tID : SV_DispatchThreadID)
{
    if (!IsInsideClusterGrid(tID))
        return;

    const uint clusterIdx = ClusterIndex(tID);

    StructuredBuffer< SceneGlobals > scene    = GetResource(g_Push.ID_Scene);
    StructuredBuffer< ClusterAABB >  clusters = GetResource(g_Push.ID_ClusterBuffer);
    ByteAddressBuffer                lights   = GetResource(scene[0].buffers.lights);

    const float4x4    mView   = scene[0].frozenCamera.mView;
    const ClusterAABB aabb    = clusters[clusterIdx];
    const float3      aabbMin = aabb.aabbMin.xyz;
    const float3      aabbMax = aabb.aabbMax.xyz;
    const LightCounts counts  = LoadLightCounts(lights);

    uint count = 0;
    [loop]
    for (uint i = 0; i < counts.numSpheres; ++i)
        if (IntersectClusterSphereLight(aabbMin, aabbMax, mView, LoadSphereLight(lights, i)))
            ++count;

    [loop]
    for (uint i = 0; i < counts.numSpots; ++i)
        if (IntersectClusterSpotLight(aabbMin, aabbMax, mView, LoadSpotLight(lights, i)))
            ++count;

    [loop]
    for (uint i = 0; i < counts.numAreas; ++i)
        if (IntersectClusterAreaLight(aabbMin, aabbMax, mView, LoadAreaLight(lights, i)))
            ++count;

    [loop]
    for (uint i = 0; i < counts.numDisks; ++i)
        if (IntersectClusterDiskLight(aabbMin, aabbMax, mView, LoadDiskLight(lights, i)))
            ++count;

    [loop]
    for (uint i = 0; i < counts.numTubes; ++i)
        if (IntersectClusterTubeLight(aabbMin, aabbMax, mView, LoadTubeLight(lights, i)))
            ++count;

    RWStructuredBuffer< uint2 > lightGrid = GetResource(g_Push.ID_LightGridBuffer);
    lightGrid[clusterIdx] = uint2(0, min(count, MAX_LIGHTS_PER_CLUSTER));
}

// Serial exclusive prefix sum over the grid
[numthreads(1, 1, 1)]
void ScanLightGrid(uint3 tID : SV_DispatchThreadID)
{
    RWStructuredBuffer< uint2 > lightGrid = GetResource(g_Push.ID_LightGridBuffer);

    const uint numClusters = g_Push.constants.numTilesX * g_Push.constants.numTilesY * CLUSTER_SLICES_Z;

    uint offset = 0;
    [loop]
    for (uint i = 0; i < numClusters; ++i)
    {
        const uint count = lightGrid[i].y;
        lightGrid[i] = uint2(offset, count);
        offset += count;
    }
}

[numthreads(4, 4, 4)]
void WriteLightList(uint3 tID : SV_DispatchThreadID)
{
    if (!IsInsideClusterGrid(tID))
        return;

    const uint clusterIdx = ClusterIndex(tID);

    StructuredBuffer< SceneGlobals > scene     = GetResource(g_Push.ID_Scene);
    StructuredBuffer< ClusterAABB >  clusters  = GetResource(g_Push.ID_ClusterBuffer);
    StructuredBuffer< uint2 >        lightGrid = GetResource(g_Push.ID_LightGridBuffer);
    RWStructuredBuffer< uint >       lightList = GetResource(g_Push.ID_LightListDataBuffer);
    ByteAddressBuffer                lights    = GetResource(scene[0].buffers.lights);

    const float4x4    mView   = scene[0].frozenCamera.mView;
    const ClusterAABB aabb    = clusters[clusterIdx];
    const float3      aabbMin = aabb.aabbMin.xyz;
    const float3      aabbMax = aabb.aabbMax.xyz;
    const LightCounts counts  = LoadLightCounts(lights);

    const uint2 cell   = lightGrid[clusterIdx];
    const uint  offset = cell.x;
    const uint  cap    = cell.y;

    uint local = 0;
    [loop]
    for (uint i = 0; i < counts.numSpheres && local < cap; ++i)
        if (IntersectClusterSphereLight(aabbMin, aabbMax, mView, LoadSphereLight(lights, i)))
            lightList[offset + local++] = EncodeLightIndex(LIGHT_TYPE_SPHERE, i);

    [loop]
    for (uint i = 0; i < counts.numSpots && local < cap; ++i)
        if (IntersectClusterSpotLight(aabbMin, aabbMax, mView, LoadSpotLight(lights, i)))
            lightList[offset + local++] = EncodeLightIndex(LIGHT_TYPE_SPOT, i);

    [loop]
    for (uint i = 0; i < counts.numAreas && local < cap; ++i)
        if (IntersectClusterAreaLight(aabbMin, aabbMax, mView, LoadAreaLight(lights, i)))
            lightList[offset + local++] = EncodeLightIndex(LIGHT_TYPE_AREA, i);

    [loop]
    for (uint i = 0; i < counts.numDisks && local < cap; ++i)
        if (IntersectClusterDiskLight(aabbMin, aabbMax, mView, LoadDiskLight(lights, i)))
            lightList[offset + local++] = EncodeLightIndex(LIGHT_TYPE_DISK, i);

    [loop]
    for (uint i = 0; i < counts.numTubes && local < cap; ++i)
        if (IntersectClusterTubeLight(aabbMin, aabbMax, mView, LoadTubeLight(lights, i)))
            lightList[offset + local++] = EncodeLightIndex(LIGHT_TYPE_TUBE, i);
}
