#include "BaambooPch.h"
#include "TerrainQuadtree.h"

namespace baamboo
{


void TerrainQuadtree::Build(const Config& cfg)
{
    m_Config = cfg;

    m_Nodes.clear();
    m_EmitList.clear();

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
    root.aabb      = BoundingBox(
        float3(cfg.rootOriginXZ.x,                 cfg.terrainMinY, cfg.rootOriginXZ.y),
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

TerrainQuadtree::Frustum TerrainQuadtree::ExtractFrustum(const mat4& viewProj)
{
    auto rowVec = [&viewProj](i32 r) -> float4
    {
        return float4(viewProj[0][r], viewProj[1][r], viewProj[2][r], viewProj[3][r]);
    };
    const float4 r0 = rowVec(0);
    const float4 r1 = rowVec(1);
    const float4 r2 = rowVec(2);
    const float4 r3 = rowVec(3);

    auto makePlane = [](const float4& v) -> Plane
    {
        Plane p;
        p.n = float3(v.x, v.y, v.z);
        p.d = v.w;
        // Normalize so .d is in world meters — Phase 3 culling distance reuse.
        const float lenSq  = glm::dot(p.n, p.n);
        const float invLen = (lenSq > 1.0e-12f) ? 1.0f / std::sqrt(lenSq) : 0.0f;
        p.n *= invLen;
        p.d *= invLen;
        return p;
    };

    Frustum f;
    f.planes[0] = makePlane(r3 + r0); // left
    f.planes[1] = makePlane(r3 - r0); // right
    f.planes[2] = makePlane(r3 + r1); // bottom
    f.planes[3] = makePlane(r3 - r1); // top
    f.planes[4] = makePlane(     r2); // near
    f.planes[5] = makePlane(r3 - r2); // far
    return f;
}

void TerrainQuadtree::SelectLOD(const float3& cameraPos, const Frustum& frustum)
{
    m_EmitList.clear();
    m_EmitList.reserve(256u);

    if (!m_Nodes.empty())
        SelectLODImpl(/*rootIdx*/ 0u, cameraPos, frustum);
}

bool TerrainQuadtree::SelectLODImpl(u32 nodeIdx, const float3& cameraPos, const Frustum& frustum)
{
    const Node& n = m_Nodes[nodeIdx];

    if (DistanceToAABB(cameraPos, n.aabb) > m_RangeEnd[n.depth])
        return false;

    if (IsAABBOutside(n.aabb, frustum))
        return true;

    if (n.depth == m_Config.maxDepth)
    {
        EmitWhole(n);
        return true;
    }

    if (DistanceToAABB(cameraPos, n.aabb) > m_RangeEnd[n.depth + 1])
    {
        EmitWhole(n);
        return true;
    }

    for (u32 c = 0u; c < 4u; ++c)
    {
        const u32 childIdx = n.childIdx[c];
        if (!SelectLODImpl(childIdx, cameraPos, frustum))
            EmitPartial(m_Nodes[childIdx], n.depth);
    }
    return true;
}

void TerrainQuadtree::EmitWhole(const Node& n)
{
    m_EmitList.push_back(PatchInstance{
        n.originXZ.x, n.originXZ.y, n.sizeMeter, n.depth, m_Config.gridN });
}

void TerrainQuadtree::EmitPartial(const Node& childNode, u32 parentDepth)
{
    m_EmitList.push_back(PatchInstance{
        childNode.originXZ.x, childNode.originXZ.y, childNode.sizeMeter,
        parentDepth, (m_Config.gridN + 1u) / 2u });
}

bool TerrainQuadtree::IsAABBOutside(const BoundingBox& aabb, const Frustum& frustum)
{
    const float3 mn = aabb.Min();
    const float3 mx = aabb.Max();
    for (i32 i = 0; i < 6; ++i)
    {
        const Plane& pl = frustum.planes[i];
        const float3 pV = float3(
            pl.n.x >= 0.0f ? mx.x : mn.x,
            pl.n.y >= 0.0f ? mx.y : mn.y,
            pl.n.z >= 0.0f ? mx.z : mn.z);

        if (glm::dot(pl.n, pV) + pl.d < 0.0f) 
            return true;
    }

    return false;
}

float TerrainQuadtree::DistanceToAABB(const float3& cameraPos, const BoundingBox& aabb)
{
    const float3 clamped = glm::clamp(cameraPos, aabb.Min(), aabb.Max());
    const float3 diff    = cameraPos - clamped;
    return glm::length(diff);
}


} // namespace baamboo
