#pragma once
#include "Boundings.h"
#include "BaambooScene/Terrain/TerrainPatch.h"

namespace baamboo
{

class TerrainQuadtree
{
public:
    // TODO. GPU-driven frustum culling
    struct Plane
    {
        float3 n;
        float  d;
    };

    struct Frustum
    {
        Plane planes[6]; // left, right, bottom, top, near, far
    };

    struct Node
    {
        BoundingBox aabb;        // world-space AABB (Phase 2 MVP: Y = global terrain Y range)
        float2      originXZ;    // world (x,z) of (u,v)=(0,0) corner
        float       sizeMeter;   // S_d = aabb.Max().x - aabb.Min().x
        u32         depth;       // 0..maxDepth (0 = root)
        u32         childIdx[4]; // 0 if leaf; else index into m_Nodes (TL, TR, BL, BR — z-msb x-lsb)
    };

    struct Config
    {
        float2 rootOriginXZ  = float2(-512.0f, -512.0f);
        float  rootSizeMeter = 1024.0f;

        float terrainMinY  = 0.0f;
        float terrainMaxY  = 2500.0f;

        float lodRangeBase = 200.0f;
        float lodMorphK    = 0.85f;
        u32   maxDepth     = 5u;
        u32   gridN        = 9u;
    };

    TerrainQuadtree() = default;

    void Build(const Config& cfg);
    void SelectLOD(const float3& cameraPos, const Frustum& frustum);

    const std::vector< PatchInstance >& EmitList()    const { return m_EmitList; }
    const std::vector< float >&         RangeStarts() const { return m_RangeStart; }
    const std::vector< float >&         RangeEnds()   const { return m_RangeEnd; }
    const Config&                       GetConfig()   const { return m_Config; }

    static Frustum ExtractFrustum(const mat4& viewProj);

private:
    void Subdivide(u32 nodeIdx);
    bool SelectLODImpl(u32 nodeIdx, const float3& cameraPos, const Frustum& frustum);

    void EmitWhole(const Node& node);
    void EmitPartial(const Node& childNode, u32 parentDepth);

    static bool  IsAABBOutside(const BoundingBox& aabb, const Frustum& frustum);
    static float DistanceToAABB(const float3& cameraPos, const BoundingBox& aabb);

private:
    Config m_Config = {};

    std::vector< Node >          m_Nodes;
    std::vector< PatchInstance > m_EmitList;   // cleared every SelectLOD
    std::vector< float >         m_RangeStart; // per-depth r_s = k * r_e
    std::vector< float >         m_RangeEnd;   // per-depth r_e
};

} // namespace baamboo
