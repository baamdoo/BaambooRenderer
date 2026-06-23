#include "BaambooPch.h"
#include "MarchingCubes.h"

namespace baamboo
{


namespace
{
	

struct EdgeVertexKey
{
    u32 x;
    u32 y;
    u32 z;
    u32 axis; // 0 = X edge, 1 = Y edge, 2 = Z edge

    bool operator==(const EdgeVertexKey& other) const
    {
        return x == other.x && y == other.y && z == other.z && axis == other.axis;
    }
};

struct EdgeVertexKeyHash
{
    size_t operator()(const EdgeVertexKey& key) const noexcept
    {
        size_t h = 1469598103934665603ull;
        auto HashCombine = [&](u32 v)
            {
                h ^= static_cast<size_t>(v);
                h *= 1099511628211ull;
            };

        HashCombine(key.x);
        HashCombine(key.y);
        HashCombine(key.z);
        HashCombine(key.axis);
        return h;
    }
};

bool IsFinite(const float3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

EdgeVertexKey MakeEdgeKey(u32 x, u32 y, u32 z, u32 edge)
{
    switch (edge)
    {
    case 0: return { x, y, z, 0 };
    case 1: return { x + 1, y, z, 1 };
    case 2: return { x, y + 1, z, 0 };
    case 3: return { x, y, z, 1 };

    case 4: return { x, y, z + 1, 0 };
    case 5: return { x + 1, y, z + 1, 1 };
    case 6: return { x, y + 1, z + 1, 0 };
    case 7: return { x, y, z + 1, 1 };

    case 8:  return { x, y, z, 2 };
    case 9:  return { x + 1, y, z, 2 };
    case 10: return { x + 1, y + 1, z, 2 };
    case 11: return { x, y + 1, z, 2 };
    }

    assert(false);
    return {};
}


// z-layer corner order in the engine's left-handed world space.
static constexpr uint3 kCornerOffset[8] = {
    { 0, 0, 0 },
    { 1, 0, 0 },
    { 1, 1, 0 },
    { 0, 1, 0 },
    { 0, 0, 1 },
    { 1, 0, 1 },
    { 1, 1, 1 },
    { 0, 1, 1 }
};

constexpr u32 kEdgeCorners[12][2] = {
    { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
	{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
	{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
};

static constexpr u32 kTriangleEdgeCount = 16u;

#include "MarchingCubesTables.inl"
static_assert(kEdgeCorners[0][0] == 0u && kEdgeCorners[0][1] == 1u);
static_assert(kEdgeCorners[8][0] == 0u && kEdgeCorners[8][1] == 4u);
static_assert(kTriangleEdgeTable[0][0] == -1);
static_assert(kTriangleEdgeTable[1][0] == 0 && kTriangleEdgeTable[1][1] == 8 && kTriangleEdgeTable[1][2] == 3);
static_assert(kTriangleEdgeTable[3][0] == 1 && kTriangleEdgeTable[3][1] == 8 && kTriangleEdgeTable[3][2] == 3);
static_assert(kTriangleEdgeTable[255][0] == -1);


}


TerrainMeshData MarchingCubes::BuildMesh(
    const SDFSampleGrid& sampleGrid,
    const VoxelTerrainChunkDesc& chunkDesc,
    const MarchingCubesBuildParams& params)
{
    auto InterpolateEdge = [](const float3& p0, const float3& p1, float v0, float v1) -> float3
        {
            if (std::abs(v0 - v1) < 1e-6f)
	    		return p0;
            return glm::mix(p0, p1, v0 / (v0 - v1));
        };

    auto IsTriangleFiniteAndNonDegenerate = [](const float3& p0, const float3& p1, const float3& p2)
        {
            if (!IsFinite(p0) || !IsFinite(p1) || !IsFinite(p2))
                return false;

            const float3 cross = glm::cross(p1 - p0, p2 - p0);
            const float crossLengthSquared = glm::dot(cross, cross);
            return std::isfinite(crossLengthSquared) && crossLengthSquared > 1e-12f;
        };

    TerrainMeshData meshData{};

    std::unordered_map< EdgeVertexKey, u32, EdgeVertexKeyHash > edgeVertexCache;
    auto GetOrCreateVertex = [&](u32 cellX, u32 cellY, u32 cellZ, u32 edge, const float3& p0, const float3& p1, float v0, float v1)
        {
            EdgeVertexKey key = MakeEdgeKey(cellX, cellY, cellZ, edge);

            if (auto it = edgeVertexCache.find(key); it != edgeVertexCache.end())
                return it->second;

            Vertex v = {};
            v.position = InterpolateEdge(p0, p1, v0, v1);
            v.normal   = float3(0.0f, 1.0f, 0.0f);
            if (params.bEstimateNormals && chunkDesc.SDF)
            {
                float3 posWorld = chunkDesc.originWorld + v.position;
                float  e = chunkDesc.settings.voxelSizeMeter * std::max(params.normalEpsilonMultiplier, 1.0e-4f);

                float dx = chunkDesc.SDF(posWorld + float3(e, 0, 0)) - chunkDesc.SDF(posWorld - float3(e, 0, 0));
                float dy = chunkDesc.SDF(posWorld + float3(0, e, 0)) - chunkDesc.SDF(posWorld - float3(0, e, 0));
                float dz = chunkDesc.SDF(posWorld + float3(0, 0, e)) - chunkDesc.SDF(posWorld - float3(0, 0, e));
                const float3 gradient = float3(dx, dy, dz);
                const float gradientLengthSquared = glm::dot(gradient, gradient);
                if (std::isfinite(gradientLengthSquared) && gradientLengthSquared > 1e-12f)
                {
                    v.normal = glm::normalize(gradient);
                }
                else
                {
                    ++meshData.numNormalGradientFallbacks;
                }
            }

            const u32 index = static_cast<u32>(meshData.vertices.size());
            meshData.vertices.push_back(v);
            edgeVertexCache[key] = index;

            return index;
        };

    const u32 cellsPerAxis = chunkDesc.settings.cellsPerAxis;
    for (u32 z = 0; z < cellsPerAxis; ++z)
    {
        for (u32 y = 0; y < cellsPerAxis; ++y)
        {
            for (u32 x = 0; x < cellsPerAxis; ++x)
            {
                bool bCellInvalid = false;

                u8 cubeIndex = 0u;
				float3 cornerPositions[8] = {};
				const SDFSample* pCornerSamples[8] = {};
                for (u32 i = 0; i < 8; ++i)
                {
                    const uint3 corner = uint3(x, y, z) + kCornerOffset[i];
                    
                    const auto pSample = sampleGrid.GetSampleLinear(corner.x, corner.y, corner.z);
                    if (!pSample || !pSample->bValid)
                    {
                        bCellInvalid = true;
                        break;
					}
                    pCornerSamples[i] = pSample;

                    cubeIndex |= (pSample->value < 0.0f ? 1u : 0u) << i;

					cornerPositions[i] = float3(corner) * chunkDesc.settings.voxelSizeMeter;
				}

                if (bCellInvalid)
					continue;

				// emit triangles for this cube
                const auto& triangleEdges = kTriangleEdgeTable[cubeIndex];
                for (u32 i = 0; triangleEdges[i] != -1; i += 3)
                {
					i32 eA = triangleEdges[i + 0];
					i32 eB = triangleEdges[i + 1];
					i32 eC = triangleEdges[i + 2];

                    const auto& edgeCornerA = kEdgeCorners[eA];
                    const auto& edgeCornerB = kEdgeCorners[eB];
                    const auto& edgeCornerC = kEdgeCorners[eC];

                    const auto pSampleA0 = pCornerSamples[edgeCornerA[0]];
                    const auto pSampleA1 = pCornerSamples[edgeCornerA[1]];
                    const auto pSampleB0 = pCornerSamples[edgeCornerB[0]];
                    const auto pSampleB1 = pCornerSamples[edgeCornerB[1]];
                    const auto pSampleC0 = pCornerSamples[edgeCornerC[0]];
                    const auto pSampleC1 = pCornerSamples[edgeCornerC[1]];
                    if (!pSampleA0 || !pSampleA0->bValid || !pSampleA1 || !pSampleA1->bValid ||
                        !pSampleB0 || !pSampleB0->bValid || !pSampleB1 || !pSampleB1->bValid ||
                        !pSampleC0 || !pSampleC0->bValid || !pSampleC1 || !pSampleC1->bValid)
                    {
                        continue;
                    }

                    const auto& samplePosA0 = cornerPositions[edgeCornerA[0]];
                    const auto& samplePosA1 = cornerPositions[edgeCornerA[1]];
                    const auto& samplePosB0 = cornerPositions[edgeCornerB[0]];
                    const auto& samplePosB1 = cornerPositions[edgeCornerB[1]];
                    const auto& samplePosC0 = cornerPositions[edgeCornerC[0]];
                    const auto& samplePosC1 = cornerPositions[edgeCornerC[1]];

                    const float3 pA = InterpolateEdge(samplePosA0, samplePosA1, pSampleA0->value, pSampleA1->value);
                    const float3 pB = InterpolateEdge(samplePosB0, samplePosB1, pSampleB0->value, pSampleB1->value);
                    const float3 pC = InterpolateEdge(samplePosC0, samplePosC1, pSampleC0->value, pSampleC1->value);
                    if (!IsTriangleFiniteAndNonDegenerate(pA, pB, pC))
                        continue;

                    u32 i0 = GetOrCreateVertex(x, y, z, eA, samplePosA0, samplePosA1, pSampleA0->value, pSampleA1->value);
                    u32 i1 = GetOrCreateVertex(x, y, z, eB, samplePosB0, samplePosB1, pSampleB0->value, pSampleB1->value);
                    u32 i2 = GetOrCreateVertex(x, y, z, eC, samplePosC0, samplePosC1, pSampleC0->value, pSampleC1->value);

                    meshData.indices.push_back(i0);
                    meshData.indices.push_back(i2);
                    meshData.indices.push_back(i1);
				}

                // debug info
                ++meshData.cubeIndexHistogram[cubeIndex];
                if (!(cubeIndex == 0 || cubeIndex == 0xFF))
                    ++meshData.numSurfaceCells;
            }
        }
	}

    meshData.RecalculateBounds();
    return meshData;
}


} // namespace baamboo
