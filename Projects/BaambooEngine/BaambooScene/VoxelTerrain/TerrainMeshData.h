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

    std::vector< Meshlet > meshlets;
    std::vector< u32 >     meshletVertices;
    std::vector< u32 >     meshletTriangles;

    u32 numSurfaceCells = 0u;
    u32 cubeIndexHistogram[256] = {};
    u32 numNormalGradientFallbacks = 0u;

    void Clear();
    void RecalculateBounds();
    bool BuildMeshlets();

    bool IsEmpty() const;
    u32  NumVertices() const;
    u32  NumIndices() const;
    u32  NumMeshlets() const;

    eVertexFormat VertexFormat() const { return eVertexFormat::P3U2N3T3; }
};


} // namespace baamboo
