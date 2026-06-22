#pragma once
#include "SDFChunk.h"
#include "VoxelTerrainTypes.h"

#include <vector>

namespace baamboo
{

class ProceduralTerrain
{
public:
    ProceduralTerrain() = default;

    void Initialize(const VoxelTerrainSettings& settings = VoxelTerrainSettings());
    void Clear();

    u32 CreateChunk(const float3& originWorld);
    u32 CreateChunk(const VoxelTerrainChunkDesc& desc);

    SDFChunk*       GetChunk(u32 chunkIndex);
    const SDFChunk* GetChunk(u32 chunkIndex) const;

    const VoxelTerrainSettings& GetSettings() const { return m_Settings; }
    const std::vector< SDFChunk >& GetChunks() const { return m_Chunks; }
    std::vector< SDFChunk >&       ChunksForWrite() { return m_Chunks; }

    bool BuildChunkSamples(u32 chunkIndex);
    bool BuildChunkMesh(u32 chunkIndex);

private:
    VoxelTerrainSettings  m_Settings = {};
    std::vector< SDFChunk > m_Chunks;
    bool                  m_bInitialized = false;
};

} // namespace baamboo
