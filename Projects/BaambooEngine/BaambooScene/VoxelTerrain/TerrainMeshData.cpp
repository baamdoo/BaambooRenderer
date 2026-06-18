#include "BaambooPch.h"
#include "TerrainMeshData.h"

namespace baamboo
{

void TerrainMeshData::Clear()
{
    vertices.clear();
    indices.clear();
    aabb       = BoundingBox(float3(0.0f), float3(0.0f));
    bHasBounds = false;
    numSurfaceCells = 0u;
    for (u32& count : cubeIndexHistogram)
        count = 0u;

    // TODO(user): Add meshlet data once terrain meshlet generation exists.
    // TODO(user): Add renderer upload handoff once StaticMeshComponent integration is ready.
}

bool TerrainMeshData::IsEmpty() const
{
    return vertices.empty() || indices.empty();
}

u32 TerrainMeshData::NumVertices() const
{
    return static_cast< u32 >(vertices.size());
}

u32 TerrainMeshData::NumIndices() const
{
    return static_cast< u32 >(indices.size());
}

} // namespace baamboo
