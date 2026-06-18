#pragma once
#include "VoxelTerrainTypes.h"

namespace baamboo
{

class ProceduralTerrain;

struct VoxelTerrainDebugStats
{
    u32 numChunks           = 0u;
    u32 numAllocatedSamples = 0u;
    u32 numValidSamples     = 0u;
    u32 numInvalidSamples   = 0u;

    u32 numSolidSamples   = 0u;
    u32 numAirSamples     = 0u;
    u32 numSurfaceSamples = 0u;

    float minSDF = 0.0f;
    float maxSDF = 0.0f;

    u32 numSurfaceCells = 0u;
    u32 numActiveCubeIndices = 0u;
    u32 cubeIndexHistogram[256] = {};

    u32 numMeshVertices = 0u;
    u32 numMeshIndices  = 0u;

    u32    numNormalVertices    = 0u;
    u32    numZeroNormals       = 0u;
    u32    numNonFiniteNormals  = 0u;
    float  minNormalLength      = 0.0f;
    float  maxNormalLength      = 0.0f;
    float  avgNormalLength      = 0.0f;
    float3 avgNormal            = float3(0.0f);
};

class VoxelTerrainDebug
{
public:
    static VoxelTerrainDebugStats CollectStats(const ProceduralTerrain& terrain);
};

} // namespace baamboo
