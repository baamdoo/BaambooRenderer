#pragma once
#include "Primitives.h"

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#define PI            (3.14159265359f)
#define PI_MUL(num)   (PI * num)
#define PI_DIV(denom) (PI / denom)

constexpr u32 MAX_BONES_PER_VERTEX = 4;
constexpr u32 MAX_BONES = 128;

using int2 = glm::ivec2;
using int3 = glm::ivec3;
using int4 = glm::ivec4;

using uint2 = glm::uvec2;
using uint3 = glm::uvec3;
using uint4 = glm::uvec4;

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;

using mat3 = glm::mat3x3;
using mat4 = glm::mat4x4;

using quat = glm::quat;

struct VertexP3
{
    float3 position;
};

struct VertexP3U2
{
    float3 position;
    float2 uv;
};

struct VertexP3U2N3
{
    float3 position;
    float2 uv;
    float3 normal;
};

struct VertexP3U2N3T3
{
    float3 position;
    float2 uv;
    float3 normal;
    float3 tangent;
};

struct VertexP3U2N3T3S
{
    float3 position;
    float2 uv;
    float3 normal;
    float3 tangent;
    u32    boneIndices; // one index per byte
    float4 boneWeights;

    // Helper methods for packing/unpacking bone indices
    void SetBoneIndex(u32 slot, u8 index)
    {
        u32 shift = slot * 8;
        u32 mask = ~(0xFF << shift);
        boneIndices = (boneIndices & mask) | (static_cast<u32>(index) << shift);
    }

    u8 GetBoneIndex(u32 slot) const
    {
        u32 shift = slot * 8;
        return static_cast<u8>((boneIndices >> shift) & 0xFF);
    }

    void SetBoneIndices(u8 index0, u8 index1, u8 index2, u8 index3)
    {
        boneIndices = (static_cast<u32>(index0) << 0) |
            (static_cast<u32>(index1) << 8) |
            (static_cast<u32>(index2) << 16) |
            (static_cast<u32>(index3) << 24);
    }
};

enum class eVertexFormat
{
    P3,          // Position only
    P3U2,        // Position + UV
    P3U2N3,      // Position + UV + Normal
    P3U2N3T3,    // Position + UV + Normal + Tangent
    P3U2N3T3S,   // Position + UV + Normal + Tangent + Skinning
};

// Helper to get vertex size
inline u32 GetVertexSize(eVertexFormat format)
{
    switch (format)
    {
    case eVertexFormat::P3:        return sizeof(VertexP3);
    case eVertexFormat::P3U2:      return sizeof(VertexP3U2);
    case eVertexFormat::P3U2N3:    return sizeof(VertexP3U2N3);
    case eVertexFormat::P3U2N3T3:  return sizeof(VertexP3U2N3T3);
    case eVertexFormat::P3U2N3T3S: return sizeof(VertexP3U2N3T3S);
    default: return 0;
    }
}

using Vertex = VertexP3U2N3T3;
using Index = u32;