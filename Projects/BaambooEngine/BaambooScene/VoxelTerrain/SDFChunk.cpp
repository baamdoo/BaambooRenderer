#include "BaambooPch.h"
#include "SDFChunk.h"

#include "MarchingCubes.h"

namespace baamboo
{


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
    desc.SDF = [center, radius](const float3& p)
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
    for (u32 z = 0; z < m_SampleGrid.GetSamplesPerAxis(); ++z)
    {
        for (u32 y = 0; y < m_SampleGrid.GetSamplesPerAxis(); ++y)
        {
            for (u32 x = 0; x < m_SampleGrid.GetSamplesPerAxis(); ++x)
            {
                SDFSample* pSample = m_SampleGrid.GetSampleLinear(x, y, z);
                if (!pSample)
                    continue;

                const float3 sampleWorldPos = m_SampleGrid.GetSampleWorldPosition(x, y, z);

                pSample->value  = m_Desc.SDF(sampleWorldPos);
                pSample->bValid = true;
            }
        }
	}

    return true;
}

bool SDFChunk::BuildMesh()
{
    MarchingCubesBuildParams params{};
    params.bEstimateNormals = static_cast< bool >(m_Desc.SDF);
    params.normalEpsilonMultiplier = m_Desc.settings.normalEpsilonMultiplier;

    m_MeshData = MarchingCubes::BuildMesh(m_SampleGrid, m_Desc, params);
    m_MeshData.BuildMeshlets();
    return true;
}


} // namespace baamboo
