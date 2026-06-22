#pragma once
#include "VoxelTerrainTypes.h"

#include <functional>

namespace baamboo
{

class ProceduralTerrain;

enum class VoxelTerrainSDFDistanceSemantics
{
    ExactEuclidean,
    DistanceLike,
};

using ExpectedOutwardNormalWorldFn = std::function< bool(const float3& positionWorld, float3& outExpectedNormal) >;

struct VoxelTerrainDebugValidationDesc
{
    const char* fixtureName = nullptr;
    VoxelTerrainSDFDistanceSemantics distanceSemantics = VoxelTerrainSDFDistanceSemantics::DistanceLike;
    bool bExpectClosedSurface = false;
    ExpectedOutwardNormalWorldFn ExpectedOutwardNormalWorld;
};

struct VoxelTerrainDebugStats
{
    const char* fixtureName = nullptr;
    VoxelTerrainSDFDistanceSemantics distanceSemantics = VoxelTerrainSDFDistanceSemantics::DistanceLike;
    bool bExpectClosedSurface = false;
    bool bHasExpectedOutwardNormal = false;

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

    u32   numFieldResidualVertices = 0u;
    u32   numNonFiniteFieldResiduals = 0u;
    float minFieldResidual = 0.0f;
    float avgFieldResidual = 0.0f;
    float maxFieldResidual = 0.0f;

    u32    numNormalVertices    = 0u;
    u32    numZeroNormals       = 0u;
    u32    numNonFiniteNormals  = 0u;
    float  minNormalLength      = 0.0f;
    float  maxNormalLength      = 0.0f;
    float  avgNormalLength      = 0.0f;
    float3 avgNormal            = float3(0.0f);

    u32   numReferenceNormalsEvaluated = 0u;
    u32   numReferenceNormalsSkipped   = 0u;
    u32   numReferenceNormalsOutward   = 0u;
    u32   numReferenceNormalsReversed  = 0u;
    float minReferenceNormalDot        = 0.0f;
    float avgReferenceNormalDot        = 0.0f;
    float maxReferenceNormalDot        = 0.0f;

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
    static VoxelTerrainDebugStats CollectStats(
        const ProceduralTerrain& terrain,
        const VoxelTerrainDebugValidationDesc* validationDesc = nullptr);
};

} // namespace baamboo
