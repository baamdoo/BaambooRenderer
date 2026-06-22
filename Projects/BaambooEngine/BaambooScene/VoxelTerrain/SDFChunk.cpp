#include "BaambooPch.h"
#include "SDFChunk.h"

#include "MarchingCubes.h"

#include <cstdio>

namespace baamboo
{

namespace
{

void LogNonFiniteSample(u32 x, u32 y, u32 z, const float3& worldPosition)
{
    std::fprintf(
        stderr,
        "SDFChunk::BuildSamples rejected non-finite field sample at (%u, %u, %u), world=(%.6f, %.6f, %.6f)\n",
        x,
        y,
        z,
        worldPosition.x,
        worldPosition.y,
        worldPosition.z);
}

} // namespace

SDFChunk::SDFChunk(const VoxelTerrainChunkDesc& desc)
{
    Initialize(desc);
}

void SDFChunk::Initialize(const VoxelTerrainChunkDesc& desc)
{
    m_Desc = desc;

    const float chunkWorldSize = m_Desc.settings.chunkWorldSizeMeter;
    m_Desc.worldBounds = BoundingBox(
        m_Desc.originWorld,
        m_Desc.originWorld + float3(chunkWorldSize, chunkWorldSize, chunkWorldSize));

    m_SampleGrid.Initialize(m_Desc.settings, m_Desc.originWorld);
    m_MeshData.Clear();
    m_bInitialized = true;
}

void SDFChunk::Initialize(const float3& originWorld, const VoxelTerrainSettings& settings)
{
    VoxelTerrainChunkDesc desc{};
    desc.originWorld = originWorld;
    desc.settings    = settings;

    const float3 center = originWorld + float3(settings.chunkWorldSizeMeter * 0.5f);
    const float  radius = settings.chunkWorldSizeMeter * 0.375f;
    desc.SDF = [center, radius](const float3& p) // default : sphere SDF
        {
            return glm::length(p - center) - radius;
        };

    Initialize(desc);
}

void SDFChunk::Clear()
{
    m_Desc = {};
    m_SampleGrid.Clear();
    m_MeshData.Clear();
    m_bInitialized = false;
}

bool SDFChunk::BuildSamples()
{
    if (!m_bInitialized || !m_SampleGrid.IsInitialized())
        return false;

    if (!m_Desc.SDF)
        return false;

    for (SDFSample& sample : m_SampleGrid.SamplesForWrite())
    {
        sample.value = 0.0f;
        sample.bValid = false;
    }

    const u32 samplesPerAxis = m_SampleGrid.GetSamplesPerAxis();
    for (u32 z = 0; z < samplesPerAxis; ++z)
    {
        for (u32 y = 0; y < samplesPerAxis; ++y)
        {
            for (u32 x = 0; x < samplesPerAxis; ++x)
            {
                SDFSample* pSample = m_SampleGrid.GetSampleLinear(x, y, z);
                if (!pSample)
                    return false;

                const float3 sampleWorldPos = m_SampleGrid.GetSampleWorldPosition(x, y, z);
                const float value = m_Desc.SDF(sampleWorldPos);
                pSample->value = value;

                if (!std::isfinite(value))
                {
                    LogNonFiniteSample(x, y, z, sampleWorldPos);
                    return false;
                }

                pSample->bValid = true;
            }
        }
    }

    return true;
}

bool SDFChunk::BuildMesh()
{
    if (!m_bInitialized || !m_SampleGrid.IsInitialized())
        return false;

    MarchingCubesBuildParams params{};
    params.bEstimateNormals = static_cast< bool >(m_Desc.SDF);
    params.normalEpsilonMultiplier = m_Desc.settings.normalEpsilonMultiplier;

    m_MeshData = MarchingCubes::BuildMesh(m_SampleGrid, m_Desc, params);

    if (m_MeshData.vertices.empty() && m_MeshData.indices.empty())
    {
        m_MeshData.meshlets.clear();
        m_MeshData.meshletVertices.clear();
        m_MeshData.meshletTriangles.clear();
        return true;
    }

    if (m_MeshData.vertices.empty() || m_MeshData.indices.empty())
        return false;

    return m_MeshData.BuildMeshlets();
}

} // namespace baamboo
