#include "BaambooPch.h"
#include "VoxelTerrainDebug.h"

#include "ProceduralTerrain.h"

#include <algorithm>
#include <cmath>

namespace baamboo
{

namespace
{

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

u64 MakeEdgeKey(u32 a, u32 b)
{
    const u32 lo = std::min(a, b);
    const u32 hi = std::max(a, b);
    return (static_cast< u64 >(lo) << 32u) | static_cast< u64 >(hi);
}

} // namespace

VoxelTerrainDebugStats VoxelTerrainDebug::CollectStats(const ProceduralTerrain& terrain)
{
    VoxelTerrainDebugStats stats{};
    stats.numChunks = static_cast< u32 >(terrain.GetChunks().size());

    bool bHasValidSample = false;
    constexpr float surfaceEpsilon = 1e-4f;
    float normalLengthSum = 0.0f;
    float3 normalSum = float3(0.0f);
    bool bHasNormal = false;
    bool bHasResidual = false;
    float residualSum = 0.0f;
    bool bHasSphereDot = false;
    float sphereDotSum = 0.0f;
    bool bHasFaceDot = false;
    float faceDotSum = 0.0f;
    bool bHasMeshBounds = false;
    std::unordered_map< u64, u32 > edgeUseCounts;

    for (const SDFChunk& chunk : terrain.GetChunks())
    {
        const SDFSampleGrid& sampleGrid = chunk.SampleGrid();
        const TerrainMeshData& meshData = chunk.MeshData();

        stats.numAllocatedSamples += sampleGrid.GetSampleCount();
        stats.numSurfaceCells     += meshData.numSurfaceCells;
        stats.numMeshVertices     += meshData.NumVertices();
        stats.numMeshIndices      += meshData.NumIndices();
        stats.numMeshlets         += meshData.NumMeshlets();

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

        const float3 sphereCenter = chunk.GetOriginWorld() + float3(chunk.GetDesc().settings.chunkWorldSizeMeter * 0.5f);
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
                        stats.minSurfaceResidual,
                        stats.maxSurfaceResidual,
                        residualSum);
                    ++stats.numResidualVertices;
                }
                else
                {
                    ++stats.numNonFiniteResiduals;
                }
            }

            const float normalLength = glm::length(vertex.normal);
            if (!IsFinite(vertex.normal) || !std::isfinite(normalLength))
            {
                ++stats.numNonFiniteNormals;
                continue;
            }

            if (normalLength <= 1e-4f)
            {
                ++stats.numZeroNormals;
                continue;
            }

            ++stats.numNormalVertices;
            normalLengthSum += normalLength;
            normalSum += vertex.normal;

            const float3 sphereOutward = vertexWorld - sphereCenter;
            const float sphereOutwardLength = glm::length(sphereOutward);
            if (std::isfinite(sphereOutwardLength) && sphereOutwardLength > 1e-6f)
            {
                const float dot = glm::dot(vertex.normal / normalLength, sphereOutward / sphereOutwardLength);
                if (std::isfinite(dot))
                {
                    AccumulateMinAvgMax(
                        dot,
                        bHasSphereDot,
                        stats.minSphereNormalDot,
                        stats.maxSphereNormalDot,
                        sphereDotSum);
                    if (dot >= 0.0f)
                        ++stats.numSphereOutwardNormals;
                    else
                        ++stats.numSphereInwardNormals;
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
            if (!IsFinite(faceCross) || !std::isfinite(faceCrossLength) || faceCrossLength <= 1e-8f)
            {
                ++stats.numDegenerateTriangles;
                continue;
            }

            edgeUseCounts[MakeEdgeKey(i0, i1)] += 1u;
            edgeUseCounts[MakeEdgeKey(i1, i2)] += 1u;
            edgeUseCounts[MakeEdgeKey(i2, i0)] += 1u;

            const float3 avgSDFNormal = v0.normal + v1.normal + v2.normal;
            const float avgSDFNormalLength = glm::length(avgSDFNormal);
            if (!IsFinite(avgSDFNormal) || !std::isfinite(avgSDFNormalLength) || avgSDFNormalLength <= 1e-6f)
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

    if (stats.numResidualVertices > 0u)
        stats.avgSurfaceResidual = residualSum / static_cast< float >(stats.numResidualVertices);

    const u32 numSphereDotNormals = stats.numSphereOutwardNormals + stats.numSphereInwardNormals;
    if (numSphereDotNormals > 0u)
        stats.avgSphereNormalDot = sphereDotSum / static_cast< float >(numSphereDotNormals);

    if (stats.numFaceNormalTriangles > 0u)
        stats.avgFaceNormalDot = faceDotSum / static_cast< float >(stats.numFaceNormalTriangles);

    for (const auto& [edgeKey, useCount] : edgeUseCounts)
    {
        UNUSED(edgeKey);
        if (useCount == 1u)
            ++stats.numBoundaryEdges;
        else if (useCount > 2u)
            ++stats.numNonManifoldEdges;
    }

    return stats;
}

} // namespace baamboo
