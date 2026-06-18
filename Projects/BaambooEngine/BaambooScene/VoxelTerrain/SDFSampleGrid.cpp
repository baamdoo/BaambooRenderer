#include "BaambooPch.h"
#include "SDFSampleGrid.h"

namespace baamboo
{

void SDFSampleGrid::Initialize(const VoxelTerrainSettings& settings, const float3& chunkOriginWorld)
{
    m_Settings         = settings;
    m_ChunkOriginWorld = chunkOriginWorld;
    m_bInitialized     = true;

    const u32 samplesPerAxis = m_Settings.samplesPerAxis;
    const u32 sampleCount    = samplesPerAxis * samplesPerAxis * samplesPerAxis;

    // Initialize storage only. SDF values are evaluated by SDFChunk::BuildSamples().
    m_Samples.assign(sampleCount, SDFSample{});
}

void SDFSampleGrid::Clear()
{
    m_Samples.clear();
    m_bInitialized = false;
}

u32 SDFSampleGrid::GetSampleCount() const
{
    return static_cast< u32 >(m_Samples.size());
}

SDFSample* SDFSampleGrid::GetSampleLinear(u32 sampleIndex)
{
    if (sampleIndex >= GetSampleCount())
        return nullptr;

    return &m_Samples[sampleIndex];
}

const SDFSample* SDFSampleGrid::GetSampleLinear(u32 sampleIndex) const
{
    if (sampleIndex >= GetSampleCount())
        return nullptr;

    return &m_Samples[sampleIndex];
}

SDFSample* SDFSampleGrid::GetSampleLinear(u32 x, u32 y, u32 z)
{
    if (x >= m_Settings.samplesPerAxis ||
        y >= m_Settings.samplesPerAxis ||
        z >= m_Settings.samplesPerAxis)
    {
        return nullptr;
    }

    return GetSampleLinear(FlattenIndex(x, y, z));
}

const SDFSample* SDFSampleGrid::GetSampleLinear(u32 x, u32 y, u32 z) const
{
    if (x >= m_Settings.samplesPerAxis ||
        y >= m_Settings.samplesPerAxis ||
        z >= m_Settings.samplesPerAxis)
    {
        return nullptr;
    }

    return GetSampleLinear(FlattenIndex(x, y, z));
}

float3 SDFSampleGrid::GetSampleWorldPosition(u32 index) const
{
    const uint3 xyz = UnflattenIndex(index);
	return GetSampleWorldPosition(xyz.x, xyz.y, xyz.z);
}

float3 SDFSampleGrid::GetSampleWorldPosition(u32 x, u32 y, u32 z) const
{
    return m_ChunkOriginWorld + 
        float3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * m_Settings.voxelSizeMeter;
}

u32 SDFSampleGrid::FlattenIndex(u32 x, u32 y, u32 z) const
{
	return (z * m_Settings.samplesPerAxis + y) * m_Settings.samplesPerAxis + x;
}

uint3 SDFSampleGrid::UnflattenIndex(u32 index) const
{
    const u32 z = index / (m_Settings.samplesPerAxis * m_Settings.samplesPerAxis);
    const u32 y = (index / m_Settings.samplesPerAxis) % m_Settings.samplesPerAxis;
    const u32 x = index % m_Settings.samplesPerAxis;
	return uint3(x, y, z);
}

} // namespace baamboo
