#pragma once

//-------------------------------------------------------------------------
// STL
//-------------------------------------------------------------------------
#include <cassert>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <thread>
#include <mutex>
#include <vector>


//-------------------------------------------------------------------------
// Defines
//-------------------------------------------------------------------------
#define UNUSED(expr) (void)(expr)

#define _KB(x) (x * 1024)
#define _MB(x) (x * 1024 * 1024)

#define _64KB _KB(64)
#define _1MB _MB(1)
#define _2MB _MB(2)
#define _4MB _MB(4)
#define _8MB _MB(8)
#define _16MB _MB(16)
#define _32MB _MB(32)
#define _64MB _MB(64)
#define _128MB _MB(128)
#define _256MB _MB(256)

#define RELEASE(obj) \
    if (obj) { \
        delete obj; \
        obj = nullptr; \
    }

#define BB_ASSERT(cond, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "Assertion failed: " __VA_ARGS__); \
            fprintf(stderr, "\nFile: %s\nLine: %d\n", __FILE__, __LINE__); \
            abort(); \
        } \
    } while (0)


//-------------------------------------------------------------------------
// Primitive Types
//-------------------------------------------------------------------------
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using f32 = float;


//-------------------------------------------------------------------------
// Math Types
//-------------------------------------------------------------------------
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


//-------------------------------------------------------------------------
// Resource Paths
//-------------------------------------------------------------------------
#define OUTPUT_PATH GetOutputPath()
inline std::string GetOutputPath()
{
    return "../../Output/";
}

#define ASSET_PATH GetAssetPath()
inline std::string GetAssetPath()
{
    return "../../Assets/";
}

#define SHADER_PATH GetShaderPath()
inline std::string GetShaderPath()
{
    return "../../Assets/Shader/";
}

#define TEXTURE_PATH GetTexturePath()
inline std::string GetTexturePath()
{
    return "../../Assets/Texture/";
}
