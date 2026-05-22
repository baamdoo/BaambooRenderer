#pragma once
#include "Primitives.h"

namespace baamboo
{

struct PatchInstance
{
    float patchOriginX; // world X of (u,v)=(0,0) corner of the rendered square
    float patchOriginZ; // world Z of same corner
    float patchSizeM;   // edge length of the rendered square in world meters
    u32   depth;        // 0..maxDepth — morph LOD range lookup in TerrainGlobals
    u32   gridDim;      // vertices per edge of THIS patch's grid
};
static_assert(sizeof(PatchInstance) == 20, "PatchInstance must stay in sync with the HLSL StructuredBuffer layout");


constexpr u32 TERRAIN_MAX_LOD_DEPTHS = 8u; // depth 0..7 inclusive

} // namespace baamboo
