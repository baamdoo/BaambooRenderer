#pragma once
#include "VoxelTerrainTypes.h"

#include <vector>

namespace baamboo
{

struct SDFSample
{
    float value  = 0.0f;
    bool  bValid = false;
};

class SDFSampleGrid
{
public:
    SDFSampleGrid() = default;

    void Initialize(const VoxelTerrainSettings& settings, const float3& chunkOriginWorld);
    void Clear();

    const VoxelTerrainSettings& GetSettings() const { return m_Settings; }
    const float3& GetChunkOriginWorld() const { return m_ChunkOriginWorld; }

    u32 GetSamplesPerAxis() const { return m_Settings.samplesPerAxis; }
    u32 GetSampleCount() const;
    bool IsInitialized() const { return m_bInitialized; }

    const std::vector< SDFSample >& Samples() const { return m_Samples; }
    std::vector< SDFSample >& SamplesForWrite() { return m_Samples; }

    SDFSample* GetSampleLinear(u32 sampleIndex);
    const SDFSample* GetSampleLinear(u32 sampleIndex) const;

    SDFSample* GetSampleLinear(u32 x, u32 y, u32 z);
    const SDFSample* GetSampleLinear(u32 x, u32 y, u32 z) const;

	float3 GetSampleWorldPosition(u32 index) const;
	float3 GetSampleWorldPosition(u32 x, u32 y, u32 z) const;

    u32 FlattenIndex(u32 x, u32 y, u32 z) const;
    uint3 UnflattenIndex(u32 index) const;

private:
    VoxelTerrainSettings m_Settings = {};
    float3               m_ChunkOriginWorld = float3(0.0f);

    std::vector< SDFSample > m_Samples;
    bool                     m_bInitialized = false;
};

} // namespace baamboo
