#pragma once
#include "VoxelTerrainTypes.h"

#include <vector>

namespace baamboo
{


struct TerrainMeshData
{
    std::vector< Vertex > vertices;
    std::vector< Index >  indices;

    BoundingBox aabb = BoundingBox(float3(0.0f), float3(0.0f));
    bool        bHasBounds = false;

    u32 numSurfaceCells = 0u;
    u32 cubeIndexHistogram[256] = {};

    void Clear();

    bool IsEmpty() const;
    u32  NumVertices() const;
    u32  NumIndices() const;

    eVertexFormat VertexFormat() const { return eVertexFormat::P3U2N3T3; }
};


} // namespace baamboo
