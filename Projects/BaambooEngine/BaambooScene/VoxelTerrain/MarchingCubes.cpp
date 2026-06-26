#include "BaambooPch.h"
#include "MarchingCubes.h"

namespace baamboo
{

namespace
{

static constexpr u32 kTriangleEdgeCount = 16u;

#include "MarchingCubesTables.inl"
static_assert(kTriangleEdgeTable[0][0] == -1);
static_assert(kTriangleEdgeTable[1][0] == 0 && kTriangleEdgeTable[1][1] == 8 && kTriangleEdgeTable[1][2] == 3);
static_assert(kTriangleEdgeTable[255][0] == -1);

}


u32 MarchingCubes::TriangleCountForCubeIndex(u32 cubeIndex)
{
    if (cubeIndex >= 256u)
        return 0u;

    const auto& row = kTriangleEdgeTable[cubeIndex];
    u32 count = 0u;
    for (u32 i = 0u; i + 2u < kTriangleEdgeCount && row[i] != -1; i += 3u)
        ++count;
    return count;
}

void MarchingCubes::FillFlatTriangleTable(i32* out4096)
{
    for (u32 ci = 0u; ci < 256u; ++ci)
        for (u32 i = 0u; i < kTriangleEdgeCount; ++i)
            out4096[ci * kTriangleEdgeCount + i] = kTriangleEdgeTable[ci][i];
}


} // namespace baamboo
