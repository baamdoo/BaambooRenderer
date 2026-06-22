#include "BaambooPch.h"
#include "ProceduralTerrain.h"

namespace baamboo
{

void ProceduralTerrain::Initialize(const VoxelTerrainSettings& settings)
{
    m_Settings     = settings;
    m_bInitialized = true;
}

void ProceduralTerrain::Clear()
{
    m_Chunks.clear();
    m_bInitialized = false;
}

u32 ProceduralTerrain::CreateChunk(const float3& originWorld)
{
    if (!m_bInitialized)
        Initialize();

    SDFChunk chunk;
    chunk.Initialize(originWorld, m_Settings);

    const u32 chunkIndex = static_cast< u32 >(m_Chunks.size());
    m_Chunks.push_back(chunk);
    return chunkIndex;
}

u32 ProceduralTerrain::CreateChunk(const VoxelTerrainChunkDesc& desc)
{
    if (!m_bInitialized)
        Initialize();

    SDFChunk chunk;
    chunk.Initialize(desc);

    const u32 chunkIndex = static_cast< u32 >(m_Chunks.size());
    m_Chunks.push_back(chunk);
    return chunkIndex;
}

SDFChunk* ProceduralTerrain::GetChunk(u32 chunkIndex)
{
    if (chunkIndex >= m_Chunks.size())
        return nullptr;

    return &m_Chunks[chunkIndex];
}

const SDFChunk* ProceduralTerrain::GetChunk(u32 chunkIndex) const
{
    if (chunkIndex >= m_Chunks.size())
        return nullptr;

    return &m_Chunks[chunkIndex];
}

bool ProceduralTerrain::BuildChunkSamples(u32 chunkIndex)
{
    SDFChunk* chunk = GetChunk(chunkIndex);
    if (!chunk)
        return false;

    return chunk->BuildSamples();
}

bool ProceduralTerrain::BuildChunkMesh(u32 chunkIndex)
{
    SDFChunk* chunk = GetChunk(chunkIndex);
    if (!chunk)
        return false;

    return chunk->BuildMesh();
}

} // namespace baamboo
