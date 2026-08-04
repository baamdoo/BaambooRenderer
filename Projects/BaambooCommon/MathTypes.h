#pragma once
#include "Primitives.h"

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define PI            (3.14159265359f)
#define PI_MUL(num)   (PI * num)
#define PI_DIV(denom) (PI / denom)

#define LOD_COUNT 8

constexpr u32 kMaxBonesPerVertex = 4;
constexpr u32 kMaxBones = 128;

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

static glm::mat4x4 perspectiveFovReverseZLH_ZO(float fov, float width, float height, float zNear, float zFar)
{
    return glm::perspectiveFovLH_ZO(fov, width, height, zFar, zNear);
};

static glm::mat4 infinitePerspectiveFovReverseZLH_ZO(float fov, float width, float height, float zNear)
{
    const float aspectRatio = width / height;

    const float h = 1.0f / glm::tan(0.5f * fov);
    const float w = h / aspectRatio;

    glm::mat4 mResult = glm::zero<glm::mat4>();
    mResult[0][0] = w;
    mResult[1][1] = h;
    mResult[2][2] = 0.0f;
    mResult[2][3] = 1.0f;
    mResult[3][2] = zNear;
    return mResult;
};


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

struct alignas(16) VertexP3U2N3T4
{
    float3 position;
    float2 uv;
    float3 normal;
    float4 tangent = float4(0.0f, 0.0f, 0.0f, 1.0f);
};
static_assert(sizeof(VertexP3U2N3T4) == 48);

struct alignas(16) VertexP3U2N3T4S
{
    float3 position;
    float2 uv;
    float3 normal;
    float4 tangent = float4(0.0f, 0.0f, 0.0f, 1.0f);

    u32    boneIndices; // one index per byte
    float4 boneWeights;
    u32    padding0 = 0;
    u32    padding1 = 0;
    u32    padding2 = 0;

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
static_assert(sizeof(VertexP3U2N3T4S) == 80);

enum class eVertexFormat
{
    P3,          // Position only
    P3U2,        // Position + UV
    P3U2N3,      // Position + UV + Normal
    P3U2N3T4,    // Position + UV + Normal + Tangent handedness
    P3U2N3T4S,   // Position + UV + Normal + Tangent handedness + Skinning
};

// Helper to get vertex size
inline u32 GetVertexSize(eVertexFormat format)
{
    switch (format)
    {
    case eVertexFormat::P3:        return sizeof(VertexP3);
    case eVertexFormat::P3U2:      return sizeof(VertexP3U2);
    case eVertexFormat::P3U2N3:    return sizeof(VertexP3U2N3);
    case eVertexFormat::P3U2N3T4:  return sizeof(VertexP3U2N3T4);
    case eVertexFormat::P3U2N3T4S: return sizeof(VertexP3U2N3T4S);
    default: return 0;
    }
}

using Vertex = VertexP3U2N3T4;
using Index = u32;

struct Meshlet
{
    u32 vertexOffset;
    u32 triangleOffset;
    u32 vertexCount;
    u32 triangleCount;

    float3 center;
    float  radius;
    float3 coneAxis;
    float  coneCutoff;
};