#include "BaambooPch.h"
#include "TerrainQuadtree.h"

namespace baamboo
{


void TerrainQuadtree::Build(const Config& cfg)
{
    m_Config = cfg;

    m_Nodes.clear();
    m_bGPUNodesDirty = true;

    m_RangeStart.assign(cfg.maxDepth + 1u, 0.0f);
    m_RangeEnd.assign(cfg.maxDepth + 1u, 0.0f);
    for (u32 d = 0u; d <= cfg.maxDepth; ++d)
    {
        const float factor = std::ldexp(1.0f, static_cast< i32 >(cfg.maxDepth - d));
        m_RangeEnd  [d] = cfg.lodRangeBase * factor;
        m_RangeStart[d] = cfg.lodMorphK * m_RangeEnd[d];
    }

    Node root{};
    root.originXZ  = cfg.rootOriginXZ;
    root.sizeMeter = cfg.rootSizeMeter;
    root.depth     = 0u;
    root.parentIdx = kInvalidIndex;
    root.aabb      = BoundingBox(
        float3(cfg.rootOriginXZ.x,                    cfg.terrainMinY, cfg.rootOriginXZ.y),
        float3(cfg.rootOriginXZ.x + cfg.rootSizeMeter, cfg.terrainMaxY, cfg.rootOriginXZ.y + cfg.rootSizeMeter));
    for (u32& ci : root.childIdx)
        ci = 0u;
    m_Nodes.push_back(root);

    if (cfg.maxDepth > 0u)
        Subdivide(0u);
}

void TerrainQuadtree::Subdivide(u32 nodeIdx)
{
    const u32 myDepth = m_Nodes[nodeIdx].depth;
    if (myDepth + 1u > m_Config.maxDepth)
        return;

    const float  halfSize = m_Nodes[nodeIdx].sizeMeter * 0.5f;
    const float2 originXZ = m_Nodes[nodeIdx].originXZ;

    const u32 childDepth = myDepth + 1u;
    const u32 firstChild = static_cast< u32 >(m_Nodes.size());
    for (u32 c = 0u; c < 4u; ++c)
    {
        const u32 cx = c & 1u;
        const u32 cz = (c >> 1u) & 1u;

        Node child{};
        child.originXZ = float2(originXZ.x + static_cast< float >(cx) * halfSize,
                                originXZ.y + static_cast< float >(cz) * halfSize);
        child.sizeMeter = halfSize;
        child.depth     = childDepth;
        child.parentIdx = nodeIdx;
        child.aabb      = BoundingBox(
            float3(child.originXZ.x,            m_Config.terrainMinY, child.originXZ.y),
            float3(child.originXZ.x + halfSize, m_Config.terrainMaxY, child.originXZ.y + halfSize));
        for (u32& gi : child.childIdx)
            gi = 0u;
        m_Nodes.push_back(child);
    }
    for (u32 c = 0u; c < 4u; ++c)
        m_Nodes[nodeIdx].childIdx[c] = firstChild + c;

    for (u32 c = 0u; c < 4u; ++c)
        Subdivide(firstChild + c);
}

const std::vector< TerrainNodeGPU >& TerrainQuadtree::GetGPUNodes() const
{
    if (m_bGPUNodesDirty)
        RebuildGPUNodes();
    return m_GPUNodes;
}

void TerrainQuadtree::RebuildGPUNodes() const
{
    m_GPUNodes.clear();
    m_GPUNodes.reserve(m_Nodes.size());

    for (const Node& n : m_Nodes)
    {
        TerrainNodeGPU g{};
        g.aabbMin     = n.aabb.Min();
        g.sizeMeter   = n.sizeMeter;
        g.aabbMax     = n.aabb.Max();
        g.depth       = n.depth;
        g.originXZ    = n.originXZ;
        g.gridN       = m_Config.gridN;
        g.parentIdx   = n.parentIdx;
        g.childIdx[0] = n.childIdx[0];
        g.childIdx[1] = n.childIdx[1];
        g.childIdx[2] = n.childIdx[2];
        g.childIdx[3] = n.childIdx[3];
        m_GPUNodes.push_back(g);
    }

    m_bGPUNodesDirty = false;
}


} // namespace baamboo
