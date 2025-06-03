#pragma once
#include "Primitives.h"

#include <glm/glm.hpp>

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

using Vertex = VertexP3U2N3T3;
using Index = u32;