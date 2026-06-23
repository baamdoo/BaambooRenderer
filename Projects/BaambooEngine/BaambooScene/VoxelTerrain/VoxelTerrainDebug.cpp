#include "BaambooPch.h"
#include "VoxelTerrainDebug.h"

#include "ProceduralTerrain.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace baamboo
{

namespace
{

bool IsFinite(const float3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

VoxelTerrainDebugStats VoxelTerrainDebug::CollectStats(const ProceduralTerrain& terrain)
{
    VoxelTerrainDebugStats stats{};
    stats.numChunks = static_cast< u32 >(terrain.GetChunks().size());

    bool bHasValidSample = false;
    bool bHasMeshBounds = false;
    bool bHasNormal = false;
    float normalLengthSum = 0.0f;
    float3 normalSum = float3(0.0f);
    std::array< bool, 256 > activeCubeIndices{};
    constexpr float surfaceEpsilon = 1.0e-4f;

    for (const SDFChunk& chunk : terrain.GetChunks())
    {
        const SDFSampleGrid& sampleGrid = chunk.SampleGrid();
        const TerrainMeshData& meshData = chunk.MeshData();

        stats.numAllocatedSamples += sampleGrid.GetSampleCount();
        stats.numSurfaceCells += meshData.numSurfaceCells;
        stats.numMeshVertices += meshData.NumVertices();
        stats.numMeshIndices += meshData.NumIndices();
        stats.numMeshlets += meshData.NumMeshlets();
        stats.numNormalGradientFallbacks += meshData.numNormalGradientFallbacks;

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
        {
            if (meshData.cubeIndexHistogram[cubeIndex] > 0u)
                activeCubeIndices[cubeIndex] = true;
        }

        for (const Vertex& vertex : meshData.vertices)
        {
            const float normalLength = glm::length(vertex.normal);
            if (!IsFinite(vertex.normal) || !std::isfinite(normalLength) || normalLength <= 1.0e-4f)
                continue;

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

    for (bool bActive : activeCubeIndices)
    {
        if (bActive)
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