#pragma once
#include "Primitives.h"

namespace baamboo
{

// GPU Transvoxel transition-cell tables (Lengyel 2009, MIT), packed into a single flat u32 SSBO
class TransvoxelTables
{
public:
    // Flat layout (u32 units):
    //   [kClassBase,      +512)  transitionCellClass  : 1 u32/case (raw byte, high bit = reversed winding)
    //   [kCellDataBase,   +560)  transitionCellData   : 10 u32/class = geometryCounts + vertexIndex[36] packed 4 bytes/u32 (LSB first)
    //   [kVertexDataBase, +3072) transitionVertexData : 6 u32/case = 12 u16 packed 2/u32 (low u16 first)
    static constexpr u32 kClassBase           = 0u;
    static constexpr u32 kCellDataBase        = 512u;
    static constexpr u32 kCellDataStrideU32   = 10u;
    static constexpr u32 kVertexDataBase      = kCellDataBase + 56u * kCellDataStrideU32;
    static constexpr u32 kVertexDataStrideU32 = 6u;
    static constexpr u32 kFlatTableSizeU32    = kVertexDataBase + 512u * kVertexDataStrideU32;

    static void FillFlatTable(u32* out4144);
};

} // namespace baamboo
