#ifndef _HLSL_VOXEL_TERRAIN_COMMON_HEADER
#define _HLSL_VOXEL_TERRAIN_COMMON_HEADER

// Voxel-terrain density field. SDF convention: solid < 0, air > 0, surface at 0.
struct VoxelTerrainGenParams
{
    float originX, originY, originZ;
    float voxelSizeMeter;

    uint  cellsPerAxis;
    uint  samplesPerAxis;
    uint  apron;
    uint  fieldMode;

    uint  heightShape;
    float baseHeightMeter;
    float slopeX;
    float slopeZ;

    float anchorXMeter;
    float anchorZMeter;
    float amplitudeMeter;
    float wavelengthXMeter;

    float wavelengthZMeter;
    uint  seed;
    float frequency;
    uint  octaves;

    float lacunarity;
    float gain;
    float warpStrength;
    float warpFrequency;

    float mountainAmplitude;
    float ridgeAmplitude;
    float ridgeFrequency;
    float caveAmplitude;

    float caveFrequency;
    float caveThreshold;
    float hardFloorY;
    float hardFloorStrength;
};

// Texel index (apron-inclusive) -> world position; interior texel (apron+i) = origin + i*voxelSize.
float3 VoxelTexelToWorld(VoxelTerrainGenParams gp, uint3 texel)
{
    float3 origin     = float3(gp.originX, gp.originY, gp.originZ);
    float3 localIndex = float3(int3(texel) - int(gp.apron));
    return origin + localIndex * gp.voxelSizeMeter;
}

// Solid AABB volume inside the chunk (SDF: < 0 inside). boxMinF/boxMaxF = fill range as chunk fractions.
float VoxelBoxField(VoxelTerrainGenParams gp, float3 p)
{
    float  chunkSize = float(gp.cellsPerAxis) * gp.voxelSizeMeter;
    float3 origin    = float3(gp.originX, gp.originY, gp.originZ);
    float3 boxMinF   = float3(0.0, 0.0, 0.0); // <-- fill from (0=chunk min)
    float3 boxMaxF   = float3(1.0, 0.5, 1.0); // <-- fill to   (1=chunk max): full XZ, lower-half Y
    float3 center    = origin + 0.5 * (boxMinF + boxMaxF) * chunkSize;
    float3 halfExt   = 0.5 * (boxMaxF - boxMinF) * chunkSize;
    float3 q         = abs(p - center) - halfExt;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// Density at a world position (SDF: solid < 0, air > 0). Box volume for now; procedural noise lands next.
float VoxelTerrainDensity(VoxelTerrainGenParams gp, float3 worldPos)
{
    return VoxelBoxField(gp, worldPos);
}

#endif // _HLSL_VOXEL_TERRAIN_COMMON_HEADER
