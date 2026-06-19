#include "BaambooPch.h"
#include "TerrainMeshData.h"

#include <meshoptimizer.h>

namespace baamboo
{

void TerrainMeshData::Clear()
{
    vertices.clear();
    indices.clear();
    meshlets.clear();
    meshletVertices.clear();
    meshletTriangles.clear();
    aabb       = BoundingBox(float3(0.0f), float3(0.0f));
    bHasBounds = false;
    numSurfaceCells = 0u;
    for (u32& count : cubeIndexHistogram)
        count = 0u;
}

void TerrainMeshData::RecalculateBounds()
{
    if (vertices.empty())
    {
        aabb = BoundingBox(float3(0.0f), float3(0.0f));
        bHasBounds = false;
        return;
    }

    float3 minPos = vertices.front().position;
    float3 maxPos = vertices.front().position;
    for (const Vertex& vertex : vertices)
    {
        minPos = glm::min(minPos, vertex.position);
        maxPos = glm::max(maxPos, vertex.position);
    }

    aabb = BoundingBox(minPos, maxPos);
    bHasBounds = true;
}

bool TerrainMeshData::BuildMeshlets()
{
    meshlets.clear();
    meshletVertices.clear();
    meshletTriangles.clear();

    if (vertices.empty() || indices.empty())
        return false;

    constexpr size_t kMaxMeshletVertices = 64u;
    constexpr size_t kMaxMeshletTriangles = 124u;
    constexpr float  kConeWeight = 0.25f;

    const size_t vertexCount = vertices.size();
    const size_t indexCount = indices.size();
    const size_t maxMeshlets = meshopt_buildMeshletsBound(indexCount, kMaxMeshletVertices, kMaxMeshletTriangles);
    if (maxMeshlets == 0u)
        return false;

    std::vector< meshopt_Meshlet > meshoptMeshlets(maxMeshlets);
    std::vector< u8 > meshletTrianglesUnpacked(maxMeshlets * kMaxMeshletTriangles * 3u);
    meshletVertices.resize(maxMeshlets * kMaxMeshletVertices);

    const size_t numMeshlets = meshopt_buildMeshlets(
        meshoptMeshlets.data(),
        meshletVertices.data(),
        meshletTrianglesUnpacked.data(),
        indices.data(),
        indexCount,
        reinterpret_cast< const float* >(vertices.data()),
        vertexCount,
        sizeof(Vertex),
        kMaxMeshletVertices,
        kMaxMeshletTriangles,
        kConeWeight);

    if (numMeshlets == 0u)
    {
        meshletVertices.clear();
        return false;
    }

    const meshopt_Meshlet& lastMeshlet = meshoptMeshlets[numMeshlets - 1u];
    meshletVertices.resize(lastMeshlet.vertex_offset + lastMeshlet.vertex_count);
    meshletTrianglesUnpacked.resize(lastMeshlet.triangle_offset + lastMeshlet.triangle_count * 3u);

    meshlets.reserve(numMeshlets);
    meshletTriangles.reserve(numMeshlets * kMaxMeshletTriangles);

    for (size_t i = 0; i < numMeshlets; ++i)
    {
        const meshopt_Meshlet& meshlet = meshoptMeshlets[i];

        meshopt_optimizeMeshlet(
            &meshletVertices[meshlet.vertex_offset],
            &meshletTrianglesUnpacked[meshlet.triangle_offset],
            meshlet.triangle_count,
            meshlet.vertex_count);

        const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            &meshletVertices[meshlet.vertex_offset],
            &meshletTrianglesUnpacked[meshlet.triangle_offset],
            meshlet.triangle_count,
            reinterpret_cast< const float* >(vertices.data()),
            vertexCount,
            sizeof(Vertex));

        Meshlet outMeshlet = {};
        outMeshlet.vertexOffset = meshlet.vertex_offset;
        outMeshlet.vertexCount = meshlet.vertex_count;
        outMeshlet.triangleCount = meshlet.triangle_count;
        outMeshlet.triangleOffset = static_cast< u32 >(meshletTriangles.size());
        outMeshlet.center = float3(bounds.center[0], bounds.center[1], bounds.center[2]);
        outMeshlet.radius = bounds.radius;
        outMeshlet.coneAxis = float3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]);
        outMeshlet.coneCutoff = bounds.cone_cutoff;

        for (size_t t = 0; t < meshlet.triangle_count; ++t)
        {
            const u8 t0 = meshletTrianglesUnpacked[meshlet.triangle_offset + t * 3u + 0u];
            const u8 t1 = meshletTrianglesUnpacked[meshlet.triangle_offset + t * 3u + 1u];
            const u8 t2 = meshletTrianglesUnpacked[meshlet.triangle_offset + t * 3u + 2u];
            meshletTriangles.push_back(u32((t2 << 16u) | (t1 << 8u) | t0));
        }

        meshlets.push_back(outMeshlet);
    }

    return true;
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

u32 TerrainMeshData::NumMeshlets() const
{
    return static_cast< u32 >(meshlets.size());
}

} // namespace baamboo
