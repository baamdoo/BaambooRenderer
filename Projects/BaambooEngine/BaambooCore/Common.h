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
// Pre-defines
//-------------------------------------------------------------------------
#define NOMINMAX

#ifdef BAAMBOO_ENGINE
#define ENGINE_API __declspec(dllexport)
#else
#define ENGINE_API __declspec(dllimport)
#endif

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

enum class eRendererAPI
{
    D3D11,
    D3D12,
    Vulkan,
    OpenGL,
    Metal,
};


//-------------------------------------------------------------------------
// Pre-declared
//-------------------------------------------------------------------------
struct SceneRenderView;

enum eComponentType
{
    TTransform = 0,
    TStaticMesh = 1,
    TDynamicMesh = 2,
    TMaterial = 3,
    TPointLight = 4,

    // ...
    NumComponents
};

struct
{
    float viewportWidth = 0;
    float viewportHeight = 0;
} static s_Data;

constexpr u32 INVALID_INDEX = 0xffffffff;
inline bool IsValidIndex(u32 index) { return index != INVALID_INDEX; }


//-------------------------------------------------------------------------
// Shader Resources
//-------------------------------------------------------------------------
using Vertex = VertexP3U2N3T3;
using Index = u32;

struct TransformData
{
    mat4 mWorld;
};

struct MaterialData 
{
    float3 tint;
    float  metallic;
    float  roughness;
    float3 padding0;

    u32 albedoID;
    u32 normalID;
    u32 specularID;
    u32 aoID;
    u32 roughnessID;
    u32 metallicID;
    u32 emissionID;
    u32 padding1;
};

struct CameraData
{
    mat4   mView;
    mat4   mProj;
    mat4   mViewProj;

    float3 position;
    float  padding0;
};


//-------------------------------------------------------------------------
// Resource Paths
//-------------------------------------------------------------------------
#include <filesystem>
namespace fs = std::filesystem;

#define OUTPUT_PATH GetOutputPath()
inline fs::path GetOutputPath()
{
    return "Output";
}

#define ASSET_PATH GetAssetPath()
inline fs::path GetAssetPath()
{
    return "Assets";
}

#define SHADER_PATH GetShaderPath()
inline fs::path GetShaderPath()
{
    return "Assets/Shader/";
}

#define TEXTURE_PATH GetTexturePath()
inline fs::path GetTexturePath()
{
    return "Assets/Texture/";
}

#define MODEL_PATH GetModelPath()
inline fs::path GetModelPath()
{
    return "Assets/Model/";
}
