#pragma once
#include "Boundings.h"
#include "Primitives.h"

namespace baamboo
{


struct TerrainNodeGPU
{
    float3 aabbMin;
    float  sizeMeter;

    float3 aabbMax;
    u32    depth;

    float2 originXZ;
    u32    gridN;
    u32    parentIdx;

    u32    childIdx[4];
};
static_assert(sizeof(TerrainNodeGPU) == 64u,
    "TerrainNodeGPU must match TerrainPatchCullingCS::TerrainNodeGPU layout (64B)");


class TerrainQuadtree
{
public:
    struct Node
    {
        BoundingBox aabb;
        float2      originXZ;
        float       sizeMeter;
        u32         depth;
        u32         parentIdx;
        u32         childIdx[4];
    };

    struct Config
    {
        float2 rootOriginXZ  = float2(-512.0f, -512.0f);
        float  rootSizeMeter = 1024.0f;

        float terrainMinY = 0.0f;
        float terrainMaxY = 2500.0f;

        float lodRangeBase = 200.0f;
        float lodMorphK    = 0.85f;
        u32   maxDepth     = 5u;
        u32   gridN        = 9u;
    };

    TerrainQuadtree() = default;

    void Build(const Config& cfg);

    const std::vector< TerrainNodeGPU >& GetGPUNodes() const;

    const std::vector< float >& RangeStarts() const { return m_RangeStart; }
    const std::vector< float >& RangeEnds()   const { return m_RangeEnd; }
    const Config&               GetConfig()   const { return m_Config; }
    u32                         NumNodes()    const { return static_cast< u32 >(m_Nodes.size()); }

private:
    void Subdivide(u32 nodeIdx);
    void RebuildGPUNodes() const;

private:
    Config m_Config = {};

    std::vector< Node >  m_Nodes;
    std::vector< float > m_RangeStart;
    std::vector< float > m_RangeEnd;

    mutable std::vector< TerrainNodeGPU > m_GPUNodes;
    mutable bool                          m_bGPUNodesDirty = true;
};

} // namespace baamboo
