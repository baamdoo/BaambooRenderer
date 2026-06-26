#pragma once
#include "Primitives.h"

namespace baamboo
{

// GPU marching-cubes lookup table (256x16 triangle-edge table)
class MarchingCubes
{
public:
    static u32 TriangleCountForCubeIndex(u32 cubeIndex);

    static constexpr u32 kFlatTriangleTableSize = 256u * 16u;
    static void FillFlatTriangleTable(i32* out4096);
};

} // namespace baamboo
