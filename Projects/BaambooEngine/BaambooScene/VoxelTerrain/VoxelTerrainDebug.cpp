#include "BaambooPch.h"
#include "VoxelTerrainDebug.h"

#include "ProceduralTerrain.h"

#include <algorithm>
#include <cmath>

namespace baamboo
{

VoxelTerrainDebugStats VoxelTerrainDebug::CollectStats(const ProceduralTerrain& terrain)
{
    VoxelTerrainDebugStats stats{};
    stats.numChunks = static_cast< u32 >(terrain.GetChunks().size());

    bool bHasValidSample = false;
    constexpr float surfaceEpsilon = 1e-4f;
    float normalLengthSum = 0.0f;
    float3 normalSum = float3(0.0f);
    bool bHasNormal = false;

    for (const SDFChunk& chunk : terrain.GetChunks())
    {
        const SDFSampleGrid& sampleGrid = chunk.SampleGrid();

        stats.numAllocatedSamples += sampleGrid.GetSampleCount();
        stats.numSurfaceCells     += chunk.MeshData().numSurfaceCells;
        stats.numMeshVertices     += chunk.MeshData().NumVertices();
        stats.numMeshIndices      += chunk.MeshData().NumIndices();

        for (u32 cubeIndex = 0u; cubeIndex < 256u; ++cubeIndex)
            stats.cubeIndexHistogram[cubeIndex] += chunk.MeshData().cubeIndexHistogram[cubeIndex];

        for (const Vertex& vertex : chunk.MeshData().vertices)
        {
            const float normalLength = glm::length(vertex.normal);
            if (!std::isfinite(vertex.normal.x) ||
                !std::isfinite(vertex.normal.y) ||
                !std::isfinite(vertex.normal.z) ||
                !std::isfinite(normalLength))
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

    return stats;
}

} // namespace baamboo
