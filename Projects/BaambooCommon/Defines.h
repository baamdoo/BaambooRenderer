#pragma once
#include <stdint.h>

#define NOMINMAX
#define NOGDI

#ifdef BB_COMMON_DLL // BaambooCommon 프로젝트를 빌드할 때 정의할 매크로
    #define BAAMBOO_API __declspec(dllexport)
    #define DLLEXPORT_TEMPLATE
#else
    #define BAAMBOO_API __declspec(dllimport)
    #define DLLEXPORT_TEMPLATE extern
#endif

#define UNUSED(expr) (void)(expr)

#define _KB(x) (x * 1024)
#define _MB(x) (x * 1024 * 1024)

#define _64KB  _KB(64)
#define _1MB   _MB(1)
#define _2MB   _MB(2)
#define _4MB   _MB(4)
#define _8MB   _MB(8)
#define _16MB  _MB(16)
#define _32MB  _MB(32)
#define _64MB  _MB(64)
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
// RendererAPI
//-------------------------------------------------------------------------
using RenderFlags = uint64_t;

#define LOD_CLAMP_NONE         (1000.0f)
#define REMAINING_MIP_LEVELS   (~0U)
#define REMAINING_ARRAY_LAYERS (~0U)
#define ALL_SUBRESOURCES       (0xFFFFFFFF)


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
