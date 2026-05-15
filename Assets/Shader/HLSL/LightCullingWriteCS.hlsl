#define _CAMERA
#define _FROZENCAMERA
#define _LIGHT
#include "Common.hlsli"
#include "LightCullingCommon.hlsli"


cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_TileSize;
    uint g_NumTilesX;
    uint g_NumTilesY;
    uint g_NumSlices;
};

ConstantBuffer< DescriptorHeapIndex > g_ClusterBuffer       : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_LightGridBuffer     : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_LightListDataBuffer : register(b3, ROOT_CONSTANT_SPACE);


[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= g_NumTilesX || DTid.y >= g_NumTilesY || DTid.z >= g_NumSlices)
        return;

    uint clusterIdx = ClusterFlatIndex(DTid, g_NumTilesX, g_NumTilesY);

    StructuredBuffer< ClusterAABB > Clusters = GetResource(g_ClusterBuffer.index);
    ClusterAABB rawAABB = Clusters[clusterIdx];

    float3 aabbMin, aabbMax;
    DecodeClusterAABB(rawAABB, aabbMin, aabbMax);

    StructuredBuffer< uint2 > LightGrid = GetResource(g_LightGridBuffer.index);
    uint  offset = LightGrid[clusterIdx].x;
    uint  cap    = LightGrid[clusterIdx].y;
    uint  local  = 0;

    RWStructuredBuffer< uint > LightListData = GetResource(g_LightListDataBuffer.index);

    [loop] for (uint si = 0; si < g_Lights.numSpheres; ++si)
    {
        if (local >= cap) return;
        SphereLight l = g_Lights.spheres[si];
        float3 cWorld = float3(l.posX, l.posY, l.posZ);
        float3 cView  = mul(g_FrozenCamera.mView, float4(cWorld, 1.0)).xyz;
        float  rMax   = InfluenceRadiusIsotropic(l.luminousFluxLm, l.radius);
        if (IntersectAabbSphere(aabbMin, aabbMax, cView, rMax))
        {
            LightListData[offset + local] = EncodeLightIndex(LIGHT_TYPE_SPHERE, si);
            ++local;
        }
    }

    [loop] for (uint pi = 0; pi < g_Lights.numSpots; ++pi)
    {
        // Spot cluster cull — sphere bound (see Count pass for rationale).
        if (local >= cap) return;
        SpotLight l = g_Lights.spots[pi];
        float3 cWorld = float3(l.posX, l.posY, l.posZ);
        float3 cView  = mul(g_FrozenCamera.mView, float4(cWorld, 1.0)).xyz;
        float  rCone  = InfluenceRadiusCone(l.luminousFluxLm, l.outerConeAngleRad, l.radiusM);
        float  rBound = rCone / max(cos(l.outerConeAngleRad), 1e-6);
        if (IntersectAabbSphere(aabbMin, aabbMax, cView, rBound))
        {
            LightListData[offset + local] = EncodeLightIndex(LIGHT_TYPE_SPOT, pi);
            ++local;
        }
    }

    [loop] for (uint ai = 0; ai < g_Lights.numAreas; ++ai)
    {
        if (local >= cap) return;
        AreaLight l = g_Lights.areas[ai];
        float3 cWorld    = float3(l.posX, l.posY, l.posZ);
        float3 nWorld    = float3(l.normalX, l.normalY, l.normalZ);
        float3 cView     = mul(g_FrozenCamera.mView, float4(cWorld, 1.0)).xyz;
        float3 nView     = mul((float3x3)g_FrozenCamera.mView, nWorld);
        float  rPhysical = sqrt(l.halfWidth * l.halfWidth + l.halfHeight * l.halfHeight);
        float  rMax      = InfluenceRadiusIsotropic(l.luminousFluxLm, rPhysical);
        if (IntersectAabbHemisphere(aabbMin, aabbMax, cView, nView, rMax))
        {
            LightListData[offset + local] = EncodeLightIndex(LIGHT_TYPE_AREA, ai);
            ++local;
        }
    }

    [loop] for (uint di = 0; di < g_Lights.numDisks; ++di)
    {
        if (local >= cap) return;
        DiskLight l = g_Lights.disks[di];
        float3 cWorld = float3(l.posX, l.posY, l.posZ);
        float3 nWorld = float3(l.normalX, l.normalY, l.normalZ);
        float3 cView  = mul(g_FrozenCamera.mView, float4(cWorld, 1.0)).xyz;
        float3 nView  = mul((float3x3)g_FrozenCamera.mView, nWorld);
        float  rMax   = InfluenceRadiusIsotropic(l.luminousFluxLm, l.radius);
        if (IntersectAabbDisk(aabbMin, aabbMax, cView, nView, rMax))
        {
            LightListData[offset + local] = EncodeLightIndex(LIGHT_TYPE_DISK, di);
            ++local;
        }
    }

    [loop] for (uint ti = 0; ti < g_Lights.numTubes; ++ti)
    {
        if (local >= cap) return;
        TubeLight l = g_Lights.tubes[ti];
        float3 aWorld = float3(l.posAX, l.posAY, l.posAZ);
        float3 bWorld = float3(l.posBX, l.posBY, l.posBZ);
        float3 aView  = mul(g_FrozenCamera.mView, float4(aWorld, 1.0)).xyz;
        float3 bView  = mul(g_FrozenCamera.mView, float4(bWorld, 1.0)).xyz;
        float  rMax   = InfluenceRadiusIsotropic(l.luminousFluxLm, l.radius);
        if (IntersectAabbCapsule(aabbMin, aabbMax, aView, bView, rMax))
        {
            LightListData[offset + local] = EncodeLightIndex(LIGHT_TYPE_TUBE, ti);
            ++local;
        }
    }
}
