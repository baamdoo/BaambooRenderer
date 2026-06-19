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
    u32 numMeshlets     = 0u;

    u32    numMeshesWithBounds = 0u;
    float3 meshBoundsMin       = float3(0.0f);
    float3 meshBoundsMax       = float3(0.0f);

    u32   numResidualVertices = 0u;
    u32   numNonFiniteResiduals = 0u;
    float minSurfaceResidual = 0.0f;
    float avgSurfaceResidual = 0.0f;
    float maxSurfaceResidual = 0.0f;

    u32    numNormalVertices    = 0u;
    u32    numZeroNormals       = 0u;
    u32    numNonFiniteNormals  = 0u;
    float  minNormalLength      = 0.0f;
    float  maxNormalLength      = 0.0f;
    float  avgNormalLength      = 0.0f;
    float3 avgNormal            = float3(0.0f);

    u32   numSphereOutwardNormals = 0u;
    u32   numSphereInwardNormals  = 0u;
    float minSphereNormalDot      = 0.0f;
    float avgSphereNormalDot      = 0.0f;
    float maxSphereNormalDot      = 0.0f;

    u32   numTriangles = 0u;
    u32   numInvalidIndexTriangles = 0u;
    u32   numDegenerateTriangles   = 0u;
    u32   numFaceNormalTriangles   = 0u;
    u32   numNegativeFaceNormalDotTriangles = 0u;
    float minFaceNormalDot = 0.0f;
    float avgFaceNormalDot = 0.0f;
    float maxFaceNormalDot = 0.0f;

    u32 numBoundaryEdges = 0u;
    u32 numNonManifoldEdges = 0u;
};

class VoxelTerrainDebug
{
public:
    static VoxelTerrainDebugStats CollectStats(const ProceduralTerrain& terrain);
};

} // namespace baamboo
