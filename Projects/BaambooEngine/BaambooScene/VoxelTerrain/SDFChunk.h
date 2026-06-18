#pragma once
#include "SDFSampleGrid.h"
#include "TerrainMeshData.h"
#include "VoxelTerrainTypes.h"

namespace baamboo
{

class SDFChunk
{
public:
    SDFChunk() = default;
    explicit SDFChunk(const VoxelTerrainChunkDesc& desc);

    void Initialize(const VoxelTerrainChunkDesc& desc);
    void Initialize(const float3& originWorld, const VoxelTerrainSettings& settings = VoxelTerrainSettings());
    void Clear();

    bool IsInitialized() const { return m_bInitialized; }

    const VoxelTerrainChunkDesc& GetDesc() const { return m_Desc; }
    const BoundingBox&           GetWorldBounds() const { return m_Desc.worldBounds; }
    const float3&                GetOriginWorld() const { return m_Desc.originWorld; }

    const SDFSampleGrid& SampleGrid() const { return m_SampleGrid; }
    SDFSampleGrid&       SampleGrid() { return m_SampleGrid; }

    const TerrainMeshData& MeshData() const { return m_MeshData; }
    TerrainMeshData&       MeshData() { return m_MeshData; }

    bool BuildSamples();
    bool BuildMesh();

private:
    VoxelTerrainChunkDesc m_Desc = {};
    SDFSampleGrid         m_SampleGrid;
    TerrainMeshData       m_MeshData;
    bool                  m_bInitialized = false;
};

} // namespace baamboo
