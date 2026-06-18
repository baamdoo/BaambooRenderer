#pragma once
#include "SDFSampleGrid.h"
#include "TerrainMeshData.h"
#include "VoxelTerrainTypes.h"

namespace baamboo
{

struct MarchingCubesBuildParams
{
    bool bEstimateNormals = false;
};

class MarchingCubes
{
public:
    static TerrainMeshData BuildMesh(
        const SDFSampleGrid&         sampleGrid,
        const VoxelTerrainChunkDesc& chunkDesc,
        const MarchingCubesBuildParams& params = MarchingCubesBuildParams());
};

} // namespace baamboo
