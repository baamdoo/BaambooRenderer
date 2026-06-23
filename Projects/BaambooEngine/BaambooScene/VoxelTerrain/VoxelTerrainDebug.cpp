#include "BaambooPch.h"
#include "VoxelTerrainDebug.h"

#include "ProceduralTerrain.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace baamboo
{

namespace
{

constexpr float kRadiansToDegrees = 57.29577951308232f;

bool IsFinite(const float3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

void AccumulateMinAvgMax(float value, bool& bHasValue, float& minValue, float& maxValue, float& sumValue)
{
    if (!bHasValue)
    {
        minValue = value;
        maxValue = value;
        bHasValue = true;
    }
    else
    {
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }
    sumValue += value;
}

float Percentile95(std::vector< float >& values)
{
    if (values.empty())
        return 0.0f;

    std::sort(values.begin(), values.end());
    const size_t index = std::min(values.size() - 1u, static_cast< size_t >(std::ceil(values.size() * 0.95f)) - 1u);
    return values[index];
}

u64 MakeEdgeKey(u32 a, u32 b)
{
    const u32 lo = std::min(a, b);
    const u32 hi = std::max(a, b);
    return (static_cast< u64 >(lo) << 32u) | static_cast< u64 >(hi);
}

void DecodeEdgeKey(u64 edgeKey, u32& outA, u32& outB)
{
    outA = static_cast< u32 >(edgeKey >> 32u);
    outB = static_cast< u32 >(edgeKey & 0xFFFFFFFFu);
}

bool IsOnBoundary(float value, float boundary, float epsilon)
{
    return std::abs(value - boundary) <= epsilon;
}

void ClassifyBoundaryEdge(
    VoxelTerrainDebugStats& stats,
    const TerrainMeshData& meshData,
    u64 edgeKey,
    float chunkSize,
    float voxelSize)
{
    u32 a = 0u;
    u32 b = 0u;
    DecodeEdgeKey(edgeKey, a, b);
    if (a >= meshData.NumVertices() || b >= meshData.NumVertices())
    {
        ++stats.numBoundaryInteriorEdges;
        return;
    }

    const float epsilon = std::max(voxelSize * 1.0e-3f, 1.0e-4f);
    const float3 p0 = meshData.vertices[a].position;
    const float3 p1 = meshData.vertices[b].position;
    if (!IsFinite(p0) || !IsFinite(p1))
    {
        ++stats.numBoundaryInteriorEdges;
        return;
    }

    const bool bSide =
        (IsOnBoundary(p0.x, 0.0f, epsilon) && IsOnBoundary(p1.x, 0.0f, epsilon)) ||
        (IsOnBoundary(p0.x, chunkSize, epsilon) && IsOnBoundary(p1.x, chunkSize, epsilon)) ||
        (IsOnBoundary(p0.z, 0.0f, epsilon) && IsOnBoundary(p1.z, 0.0f, epsilon)) ||
        (IsOnBoundary(p0.z, chunkSize, epsilon) && IsOnBoundary(p1.z, chunkSize, epsilon));
    if (bSide)
    {
        ++stats.numBoundarySideEdges;
        return;
    }

    const bool bTopBottom =
        (IsOnBoundary(p0.y, 0.0f, epsilon) && IsOnBoundary(p1.y, 0.0f, epsilon)) ||
        (IsOnBoundary(p0.y, chunkSize, epsilon) && IsOnBoundary(p1.y, chunkSize, epsilon));
    if (bTopBottom)
    {
        ++stats.numBoundaryTopBottomEdges;
        return;
    }

    ++stats.numBoundaryInteriorEdges;
}

void CollectSurfaceSignProbes(
    VoxelTerrainDebugStats& stats,
    const SDFChunk& chunk,
    const VoxelTerrainDebugValidationDesc& validationDesc)
{
    const VoxelTerrainChunkDesc& chunkDesc = chunk.GetDesc();
    if (!validationDesc.bProbeSurfaceSigns || !validationDesc.SurfaceHeightWorld || !chunkDesc.SDF)
        return;

    const u32 samplesPerAxis = chunkDesc.settings.samplesPerAxis;
    const float voxelSize = chunkDesc.settings.voxelSizeMeter;
    const float offset = voxelSize * 0.5f;

    for (u32 z = 0u; z < samplesPerAxis; ++z)
    {
        for (u32 x = 0u; x < samplesPerAxis; ++x)
        {
            const float xWorld = chunkDesc.originWorld.x + static_cast< float >(x) * voxelSize;
            const float zWorld = chunkDesc.originWorld.z + static_cast< float >(z) * voxelSize;

            float heightWorld = 0.0f;
            if (!validationDesc.SurfaceHeightWorld(xWorld, zWorld, heightWorld) || !std::isfinite(heightWorld))
            {
                ++stats.numNonFiniteSurfaceSignProbes;
                continue;
            }

            const float below = chunkDesc.SDF(float3(xWorld, heightWorld - offset, zWorld));
            const float above = chunkDesc.SDF(float3(xWorld, heightWorld + offset, zWorld));
            if (!std::isfinite(below) || !std::isfinite(above))
            {
                ++stats.numNonFiniteSurfaceSignProbes;
                continue;
            }

            ++stats.numSurfaceSignProbePairs;
            if (!(below < 0.0f && above > 0.0f))
                ++stats.numSurfaceSignProbeFailures;
        }
    }
}

void CollectSharedFaceProbes(
    VoxelTerrainDebugStats& stats,
    const SDFChunk& chunk,
    const VoxelTerrainDebugValidationDesc& validationDesc)
{
    const VoxelTerrainChunkDesc& chunkDesc = chunk.GetDesc();
    if (!validationDesc.bProbeSharedFaceContinuity || !validationDesc.ReferenceFieldWorld || !chunkDesc.SDF)
        return;

    const u32 samplesPerAxis = chunkDesc.settings.samplesPerAxis;
    const float voxelSize = chunkDesc.settings.voxelSizeMeter;
    const float chunkSize = chunkDesc.settings.chunkWorldSizeMeter;

    auto Probe = [&](const float3& positionFromThisChunk, const float3& positionFromNeighborChunk)
        {
            const float positionDelta = glm::length(positionFromThisChunk - positionFromNeighborChunk);
            if (std::isfinite(positionDelta))
                stats.maxSharedFacePositionDelta = std::max(stats.maxSharedFacePositionDelta, positionDelta);

            float referenceValue = 0.0f;
            const float chunkValue = chunkDesc.SDF(positionFromThisChunk);
            if (!validationDesc.ReferenceFieldWorld(positionFromNeighborChunk, referenceValue) ||
                !std::isfinite(chunkValue) ||
                !std::isfinite(referenceValue))
            {
                ++stats.numNonFiniteSharedFaceProbes;
                return;
            }

            ++stats.numSharedFaceProbeSamples;
            stats.maxSharedFaceFieldDelta = std::max(stats.maxSharedFaceFieldDelta, std::abs(chunkValue - referenceValue));
        };

    for (u32 b = 0u; b < samplesPerAxis; ++b)
    {
        for (u32 a = 0u; a < samplesPerAxis; ++a)
        {
            const float u = static_cast< float >(a) * voxelSize;
            const float v = static_cast< float >(b) * voxelSize;

            const float3 positiveXFromThis = chunkDesc.originWorld + float3(chunkSize, u, v);
            const float3 positiveXNeighborOrigin = chunkDesc.originWorld + float3(chunkSize, 0.0f, 0.0f);
            Probe(positiveXFromThis, positiveXNeighborOrigin + float3(0.0f, u, v));

            const float3 positiveZFromThis = chunkDesc.originWorld + float3(u, v, chunkSize);
            const float3 positiveZNeighborOrigin = chunkDesc.originWorld + float3(0.0f, 0.0f, chunkSize);
            Probe(positiveZFromThis, positiveZNeighborOrigin + float3(u, v, 0.0f));
        }
    }
}

} // namespace

VoxelTerrainDebugStats VoxelTerrainDebug::CollectStats(
    const ProceduralTerrain& terrain,
    const VoxelTerrainDebugValidationDesc* validationDesc)
{
    VoxelTerrainDebugStats stats{};
    stats.numChunks = static_cast< u32 >(terrain.GetChunks().size());

    if (validationDesc)
    {
        stats.fixtureName = validationDesc->fixtureName;
        stats.distanceSemantics = validationDesc->distanceSemantics;
        stats.bExpectClosedSurface = validationDesc->bExpectClosedSurface;
        stats.bHasExpectedOutwardNormal = static_cast< bool >(validationDesc->ExpectedOutwardNormalWorld);
    }

    bool bHasValidSample = false;
    constexpr float surfaceEpsilon = 1.0e-4f;
    float normalLengthSum = 0.0f;
    float3 normalSum = float3(0.0f);
    bool bHasNormal = false;
    bool bHasResidual = false;
    float residualSum = 0.0f;
    std::vector< float > fieldResiduals;
    bool bHasMetricResidual = false;
    float metricResidualSum = 0.0f;
    std::vector< float > metricResiduals;
    bool bHasReferenceNormalDot = false;
    float referenceNormalDotSum = 0.0f;
    bool bHasReferenceNormalAngle = false;
    float referenceNormalAngleSum = 0.0f;
    bool bHasFaceDot = false;
    float faceDotSum = 0.0f;
    bool bHasMeshBounds = false;

    for (const SDFChunk& chunk : terrain.GetChunks())
    {
        const SDFSampleGrid& sampleGrid = chunk.SampleGrid();
        const TerrainMeshData& meshData = chunk.MeshData();

        stats.numAllocatedSamples += sampleGrid.GetSampleCount();
        stats.numSurfaceCells     += meshData.numSurfaceCells;
        stats.numMeshVertices     += meshData.NumVertices();
        stats.numMeshIndices      += meshData.NumIndices();
        stats.numMeshlets         += meshData.NumMeshlets();
        stats.numNormalGradientFallbacks += meshData.numNormalGradientFallbacks;
        if (meshData.NumIndices() % 3u != 0u)
            ++stats.numMalformedIndexBuffers;

        if (meshData.bHasBounds)
        {
            ++stats.numMeshesWithBounds;
            if (!bHasMeshBounds)
            {
                stats.meshBoundsMin = meshData.aabb.Min();
                stats.meshBoundsMax = meshData.aabb.Max();
                bHasMeshBounds = true;
            }
            else
            {
                stats.meshBoundsMin = glm::min(stats.meshBoundsMin, meshData.aabb.Min());
                stats.meshBoundsMax = glm::max(stats.meshBoundsMax, meshData.aabb.Max());
            }
        }

        for (u32 cubeIndex = 0u; cubeIndex < 256u; ++cubeIndex)
            stats.cubeIndexHistogram[cubeIndex] += meshData.cubeIndexHistogram[cubeIndex];

        for (const Vertex& vertex : meshData.vertices)
        {
            const float3 vertexWorld = chunk.GetOriginWorld() + vertex.position;
            if (chunk.GetDesc().SDF)
            {
                const float residual = std::abs(chunk.GetDesc().SDF(vertexWorld));
                if (std::isfinite(residual))
                {
                    AccumulateMinAvgMax(
                        residual,
                        bHasResidual,
                        stats.minFieldResidual,
                        stats.maxFieldResidual,
                        residualSum);
                    fieldResiduals.push_back(residual);
                    ++stats.numFieldResidualVertices;
                }
                else
                {
                    ++stats.numNonFiniteFieldResiduals;
                }
            }

            if (validationDesc && validationDesc->MetricResidualWorld)
            {
                float metricResidual = 0.0f;
                if (validationDesc->MetricResidualWorld(vertexWorld, metricResidual) && std::isfinite(metricResidual))
                {
                    AccumulateMinAvgMax(
                        metricResidual,
                        bHasMetricResidual,
                        stats.minMetricResidual,
                        stats.maxMetricResidual,
                        metricResidualSum);
                    metricResiduals.push_back(metricResidual);
                    ++stats.numMetricResidualVertices;
                }
                else
                {
                    ++stats.numNonFiniteMetricResiduals;
                }
            }

            const float normalLength = glm::length(vertex.normal);
            if (!IsFinite(vertex.normal) || !std::isfinite(normalLength))
            {
                ++stats.numNonFiniteNormals;
                continue;
            }

            if (normalLength <= 1.0e-4f)
            {
                ++stats.numZeroNormals;
                continue;
            }

            ++stats.numNormalVertices;
            normalLengthSum += normalLength;
            normalSum += vertex.normal;

            if (validationDesc && validationDesc->ExpectedOutwardNormalWorld)
            {
                float3 expectedNormal = float3(0.0f);
                if (!validationDesc->ExpectedOutwardNormalWorld(vertexWorld, expectedNormal))
                {
                    ++stats.numReferenceNormalsSkipped;
                }
                else
                {
                    const float expectedNormalLength = glm::length(expectedNormal);
                    if (!IsFinite(expectedNormal) || !std::isfinite(expectedNormalLength) || expectedNormalLength <= 1.0e-6f)
                    {
                        ++stats.numReferenceNormalsSkipped;
                    }
                    else
                    {
                        const float rawDot = glm::dot(vertex.normal / normalLength, expectedNormal / expectedNormalLength);
                        const float dot = glm::clamp(rawDot, -1.0f, 1.0f);
                        if (std::isfinite(dot))
                        {
                            AccumulateMinAvgMax(
                                dot,
                                bHasReferenceNormalDot,
                                stats.minReferenceNormalDot,
                                stats.maxReferenceNormalDot,
                                referenceNormalDotSum);
                            ++stats.numReferenceNormalsEvaluated;
                            if (dot >= 0.0f)
                                ++stats.numReferenceNormalsOutward;
                            else
                                ++stats.numReferenceNormalsReversed;

                            const float angleDegree = std::acos(dot) * kRadiansToDegrees;
                            if (std::isfinite(angleDegree))
                            {
                                AccumulateMinAvgMax(
                                    angleDegree,
                                    bHasReferenceNormalAngle,
                                    stats.minReferenceNormalAngleDegree,
                                    stats.maxReferenceNormalAngleDegree,
                                    referenceNormalAngleSum);
                            }
                        }
                        else
                        {
                            ++stats.numReferenceNormalsSkipped;
                        }
                    }
                }
            }

            if (!bHasNormal)
            {
                stats.minNormalLength = normalLength;
                stats.maxNormalLength = normalLength;
                bHasNormal = true;
            }
            else
            {
                stats.minNormalLength = std::min(stats.minNormalLength, normalLength);
                stats.maxNormalLength = std::max(stats.maxNormalLength, normalLength);
            }
        }

        std::unordered_map< u64, u32 > edgeUseCounts;
        for (u32 index = 0u; index + 2u < meshData.NumIndices(); index += 3u)
        {
            ++stats.numTriangles;

            const u32 i0 = meshData.indices[index + 0u];
            const u32 i1 = meshData.indices[index + 1u];
            const u32 i2 = meshData.indices[index + 2u];
            if (i0 >= meshData.NumVertices() || i1 >= meshData.NumVertices() || i2 >= meshData.NumVertices())
            {
                ++stats.numInvalidIndexTriangles;
                continue;
            }

            if (i0 == i1 || i1 == i2 || i2 == i0)
            {
                ++stats.numDegenerateTriangles;
                continue;
            }

            const Vertex& v0 = meshData.vertices[i0];
            const Vertex& v1 = meshData.vertices[i1];
            const Vertex& v2 = meshData.vertices[i2];
            const float3 p0 = v0.position;
            const float3 p1 = v1.position;
            const float3 p2 = v2.position;

            const float3 faceCross = glm::cross(p1 - p0, p2 - p0);
            const float faceCrossLength = glm::length(faceCross);
            if (!IsFinite(faceCross) || !std::isfinite(faceCrossLength) || faceCrossLength <= 1.0e-8f)
            {
                ++stats.numDegenerateTriangles;
                continue;
            }

            edgeUseCounts[MakeEdgeKey(i0, i1)] += 1u;
            edgeUseCounts[MakeEdgeKey(i1, i2)] += 1u;
            edgeUseCounts[MakeEdgeKey(i2, i0)] += 1u;

            const float3 avgSDFNormal = v0.normal + v1.normal + v2.normal;
            const float avgSDFNormalLength = glm::length(avgSDFNormal);
            if (!IsFinite(avgSDFNormal) || !std::isfinite(avgSDFNormalLength) || avgSDFNormalLength <= 1.0e-6f)
                continue;

            const float dot = glm::dot(faceCross / faceCrossLength, avgSDFNormal / avgSDFNormalLength);
            if (!std::isfinite(dot))
                continue;

            AccumulateMinAvgMax(
                dot,
                bHasFaceDot,
                stats.minFaceNormalDot,
                stats.maxFaceNormalDot,
                faceDotSum);
            ++stats.numFaceNormalTriangles;
            if (dot < 0.0f)
                ++stats.numNegativeFaceNormalDotTriangles;
        }

        for (const auto& [edgeKey, useCount] : edgeUseCounts)
        {
            if (useCount == 1u)
            {
                ++stats.numBoundaryEdges;
                ClassifyBoundaryEdge(
                    stats,
                    meshData,
                    edgeKey,
                    chunk.GetDesc().settings.chunkWorldSizeMeter,
                    chunk.GetDesc().settings.voxelSizeMeter);
            }
            else if (useCount > 2u)
            {
                ++stats.numNonManifoldEdges;
            }
        }

        for (const SDFSample& sample : sampleGrid.Samples())
        {
            if (!sample.bValid)
            {
                ++stats.numInvalidSamples;
                continue;
            }

            ++stats.numValidSamples;

            if (!bHasValidSample)
            {
                stats.minSDF = sample.value;
                stats.maxSDF = sample.value;
                bHasValidSample = true;
            }
            else
            {
                stats.minSDF = std::min(stats.minSDF, sample.value);
                stats.maxSDF = std::max(stats.maxSDF, sample.value);
            }

            if (sample.value < -surfaceEpsilon)
                ++stats.numSolidSamples;
            else if (sample.value > surfaceEpsilon)
                ++stats.numAirSamples;
            else
                ++stats.numSurfaceSamples;
        }

        if (validationDesc)
        {
            CollectSurfaceSignProbes(stats, chunk, *validationDesc);
            CollectSharedFaceProbes(stats, chunk, *validationDesc);
        }
    }

    for (u32 cubeIndex = 0u; cubeIndex < 256u; ++cubeIndex)
    {
        if (stats.cubeIndexHistogram[cubeIndex] > 0u)
            ++stats.numActiveCubeIndices;
    }

    if (stats.numNormalVertices > 0u)
    {
        const float invNormalCount = 1.0f / static_cast< float >(stats.numNormalVertices);
        stats.avgNormalLength = normalLengthSum * invNormalCount;
        stats.avgNormal = normalSum * invNormalCount;
    }

    if (stats.numFieldResidualVertices > 0u)
    {
        stats.avgFieldResidual = residualSum / static_cast< float >(stats.numFieldResidualVertices);
        stats.p95FieldResidual = Percentile95(fieldResiduals);
    }

    if (stats.numMetricResidualVertices > 0u)
    {
        stats.avgMetricResidual = metricResidualSum / static_cast< float >(stats.numMetricResidualVertices);
        stats.p95MetricResidual = Percentile95(metricResiduals);
    }

    if (stats.numReferenceNormalsEvaluated > 0u)
    {
        stats.avgReferenceNormalDot = referenceNormalDotSum / static_cast< float >(stats.numReferenceNormalsEvaluated);
        if (bHasReferenceNormalAngle)
            stats.avgReferenceNormalAngleDegree = referenceNormalAngleSum / static_cast< float >(stats.numReferenceNormalsEvaluated);
    }

    if (stats.numFaceNormalTriangles > 0u)
        stats.avgFaceNormalDot = faceDotSum / static_cast< float >(stats.numFaceNormalTriangles);

    return stats;
}

} // namespace baamboo