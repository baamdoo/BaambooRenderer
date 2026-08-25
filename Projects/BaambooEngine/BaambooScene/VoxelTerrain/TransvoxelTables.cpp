#include "BaambooPch.h"
#include "TransvoxelTables.h"

namespace baamboo
{

namespace
{

#include "TransvoxelTables.inl"

// FNV-1a over the transcription's byte serialization; guards the table data against accidental edits
constexpr u32 Fnv1a(u32 h, u32 byte) { return (h ^ byte) * 0x01000193u; }

constexpr u32 HashClassTable()
{
    u32 h = 0x811C9DC5u;
    for (u32 c = 0u; c < 512u; ++c)
        h = Fnv1a(h, transitionCellClass[c]);
    return h;
}

constexpr u32 HashCellData()
{
    u32 h = 0x811C9DC5u;
    for (u32 c = 0u; c < 56u; ++c)
    {
        h = Fnv1a(h, u32(transitionCellData[c].geometryCounts));
        for (u32 i = 0u; i < 36u; ++i)
            h = Fnv1a(h, transitionCellData[c].vertexIndex[i]);
    }
    return h;
}

constexpr u32 HashVertexData()
{
    u32 h = 0x811C9DC5u;
    for (u32 c = 0u; c < 512u; ++c)
    {
        for (u32 i = 0u; i < 12u; ++i)
        {
            h = Fnv1a(h, transitionVertexData[c][i] & 0xFFu);
            h = Fnv1a(h, u32(transitionVertexData[c][i]) >> 8u);
        }
    }
    return h;
}

static_assert(sizeof(transitionCellClass) == 512);
static_assert(sizeof(transitionCellData) / sizeof(transitionCellData[0]) == 56);
static_assert(sizeof(transitionCornerData) == 13);
static_assert(sizeof(transitionVertexData) / sizeof(transitionVertexData[0]) == 512);
static_assert(sizeof(transitionVertexData[0]) / sizeof(unsigned short) == 12);
static_assert(transitionCellClass[0] == 0x00 && transitionCellClass[3] == 0x84);
static_assert(transitionCellData[0].geometryCounts == 0x00 && transitionVertexData[0][0] == 0);
static_assert(transitionCornerData[0] == 0x30 && transitionCornerData[12] == 0x87);
static_assert(HashClassTable() == 0x77F24F7Fu);
static_assert(HashCellData() == 0x3A2C42DFu);
static_assert(HashVertexData() == 0x0849EC85u);

}


void TransvoxelTables::FillFlatTable(u32* out4144)
{
    u32* p = out4144 + kClassBase;
    for (u32 c = 0u; c < 512u; ++c)
        p[c] = transitionCellClass[c];

    p = out4144 + kCellDataBase;
    for (u32 c = 0u; c < 56u; ++c, p += kCellDataStrideU32)
    {
        p[0] = u32(transitionCellData[c].geometryCounts);
        for (u32 w = 0u; w < 9u; ++w)
        {
            const unsigned char* b = transitionCellData[c].vertexIndex + w * 4u;
            p[1u + w] = u32(b[0]) | (u32(b[1]) << 8u) | (u32(b[2]) << 16u) | (u32(b[3]) << 24u);
        }
    }

    p = out4144 + kVertexDataBase;
    for (u32 c = 0u; c < 512u; ++c, p += kVertexDataStrideU32)
    {
        for (u32 w = 0u; w < 6u; ++w)
            p[w] = u32(transitionVertexData[c][2u * w]) | (u32(transitionVertexData[c][2u * w + 1u]) << 16u);
    }
}


} // namespace baamboo
