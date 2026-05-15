#define _FROZENCAMERA
#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_TileSize;    // px (CLUSTER_TILE_SIZE_PX = 64)
    uint g_NumTilesX;
    uint g_NumTilesY;
    uint g_NumSlices;   // CLUSTER_SLICES_Z = 32
};

ConstantBuffer< DescriptorHeapIndex > g_ClusterBuffer : register(b1, ROOT_CONSTANT_SPACE);


float2 PixelToNDC(float2 p)
{
    float W = g_NumTilesX * g_TileSize;
    float H = g_NumTilesY * g_TileSize;

    return float2(2.0 * (p.x / W) - 1.0, 1.0 - 2.0 * (p.y / H));
}

[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= g_NumTilesX || DTid.y >= g_NumTilesY || DTid.z >= g_NumSlices)
		return;

    uint tid = (DTid.z * g_NumTilesY + DTid.y) * g_NumTilesX + DTid.x;

    float2 tileMin = float2(DTid.xy) * float(g_TileSize);
    float2 tileMax = tileMin + float(g_TileSize);

    float2 ndcLT = PixelToNDC(float2(tileMin.x, tileMin.y));
    float2 ndcLB = PixelToNDC(float2(tileMin.x, tileMax.y));
    float2 ndcRB = PixelToNDC(float2(tileMax.x, tileMax.y));
	float2 ndcRT = PixelToNDC(float2(tileMax.x, tileMin.y));

	float zNear = g_FrozenCamera.zNear;
    float zFar  = g_FrozenCamera.zFar;
	// log(z) = lerp(log(zNear), log(zFar), k/Nz) => z = exp(lerp(log(zNear), log(zFar), k/Nz)) => z = zNear * pow(zFar/zNear, k/Nz)
	float zSliceNear = zNear * pow(zFar / zNear, DTid.z / float(g_NumSlices));
	float zSliceFar  = zNear * pow(zFar / zNear, (DTid.z + 1) / float(g_NumSlices));

	float P00 = g_FrozenCamera.mProj[0][0];
	float P11 = g_FrozenCamera.mProj[1][1];
    float3 cornersVS[8];
    cornersVS[0] = float3(ndcLT.x * zSliceNear / P00, ndcLT.y * zSliceNear / P11, zSliceNear); // near left-top
    cornersVS[1] = float3(ndcLB.x * zSliceNear / P00, ndcLB.y * zSliceNear / P11, zSliceNear); // near left-bottom
    cornersVS[2] = float3(ndcRB.x * zSliceNear / P00, ndcRB.y * zSliceNear / P11, zSliceNear); // near right-bottom
    cornersVS[3] = float3(ndcRT.x * zSliceNear / P00, ndcRT.y * zSliceNear / P11, zSliceNear); // near right-top
    cornersVS[4] = float3(ndcLT.x * zSliceFar / P00, ndcLT.y * zSliceFar / P11, zSliceFar);    // far left-top
    cornersVS[5] = float3(ndcLB.x * zSliceFar / P00, ndcLB.y * zSliceFar / P11, zSliceFar);    // far left-bottom
    cornersVS[6] = float3(ndcRB.x * zSliceFar / P00, ndcRB.y * zSliceFar / P11, zSliceFar);    // far right-bottom
    cornersVS[7] = float3(ndcRT.x * zSliceFar / P00, ndcRT.y * zSliceFar / P11, zSliceFar);    // far right-top

    float3 aabbMin = FLT_MAX, aabbMax = -FLT_MAX;
    for (int i = 0; i < 8; ++i)
    {
        aabbMin = min(aabbMin, cornersVS[i]);
        aabbMax = max(aabbMax, cornersVS[i]);
	}

    ClusterAABB aabb;
	aabb.minX = aabbMin.x; aabb.minY = aabbMin.y; aabb.minZ = aabbMin.z; aabb.minW = 0.0;
	aabb.maxX = aabbMax.x; aabb.maxY = aabbMax.y; aabb.maxZ = aabbMax.z; aabb.maxW = 0.0;

	RWStructuredBuffer< ClusterAABB > Cluster = GetResource(g_ClusterBuffer.index);
    Cluster[tid] = aabb;
}
