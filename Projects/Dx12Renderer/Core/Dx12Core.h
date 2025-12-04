#pragma once
#include <cstdint>
#include <codecvt>
#include <locale>
#include <comdef.h>


//-------------------------------------------------------------------------
// Header
//-------------------------------------------------------------------------
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d11on12.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>


//-------------------------------------------------------------------------
// Libraries
//-------------------------------------------------------------------------
#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")


//-------------------------------------------------------------------------
// SDK
//-------------------------------------------------------------------------
extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; }

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 614; }

#if defined(_M_ARM64EC)
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\arm64\\"; }
#elif defined(_M_ARM64)
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\arm64\\"; }
#elif defined(_M_AMD64)
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\x64\\"; }
#elif defined(_M_IX86)
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\x86\\"; }
#endif


//-------------------------------------------------------------------------
// Assertion
//-------------------------------------------------------------------------
#define DX_CHECK(value) ThrowIfFailed(value)
inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        _com_error err(hr);
        OutputDebugString(err.ErrorMessage());

        __debugbreak();
    }
}


//-------------------------------------------------------------------------
// Resource Paths
//-------------------------------------------------------------------------
#include <filesystem>
namespace fs = std::filesystem;

#define CSO_PATH GetCsoPath()
inline fs::path GetCsoPath()
{
    return "Output/Shader/cso/";
}


//-------------------------------------------------------------------------
// Helpers
//-------------------------------------------------------------------------
#define COM_RELEASE(obj) \
    if (obj) { \
        obj->Release(); \
        obj = nullptr; \
    }

#if defined (_DEBUG)
#include <dxgidebug.h>
inline void ReportLiveObjects()
{
    IDXGIDebug1* dxgiDebug;
    DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug));

    dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
    dxgiDebug->Release();
}
#endif

inline std::wstring ConvertToWString(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(str);
}


//-------------------------------------------------------------------------
// Predefined
//-------------------------------------------------------------------------
constexpr u32 NUM_FRAMES_IN_FLIGHT = 3u;
constexpr u32 NUM_SAMPLING = 1u;
constexpr u32 NUM_RESOURCE_DESCRIPTOR_TYPE = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER + 1;
constexpr u32 MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = { 1024, 32, 256, 8 };
constexpr u32 MAX_GLOBAL_DESCRIPTORS = 8192;
constexpr u32 ROOT_CONSTANT_SPACE = 100;
constexpr u32 MAX_ROOT_INDEX = D3D12_MAX_ROOT_COST;
constexpr u32 MAX_ROOTCONSTANTS = 32u;
constexpr u32 MAX_DESCRIPTORHEAPINDICES = 16u;
constexpr u32 MAX_LOCAL_ROOTCONSTANTS = MAX_ROOTCONSTANTS - MAX_DESCRIPTORHEAPINDICES;
constexpr u32 MAX_CBVS = (MAX_ROOT_INDEX - MAX_ROOTCONSTANTS) / 2;

constexpr u32 GLOBAL_DESCRIPTOR_SPACE = 0u;

enum class eSamplerIndex
{
	LinearClamp     = 0,
	LinearWrap      = 1,
	PointClamp      = 2,
	PointWrap       = 3,
	TrilinearWrap   = 4,
	AnisotropicWrap = 5,

	ShadowCmpLessEqual = 6,

	MaxIndex,
};


//-------------------------------------------------------------------------
// Shader Types
//-------------------------------------------------------------------------
//#pragma pack(push, 4)
struct IndirectDrawData
{
    u32 transformID;
    u32 materialID;

    D3D12_VERTEX_BUFFER_VIEW vbv;
    D3D12_INDEX_BUFFER_VIEW  ibv;

    D3D12_DRAW_INDEXED_ARGUMENTS draws;
};
//#pragma pack(pop)


//-------------------------------------------------------------------------
// Render Context
//-------------------------------------------------------------------------
#include "RenderDevice/Dx12RenderDevice.h"


//-------------------------------------------------------------------------
// Render Resource
//-------------------------------------------------------------------------
#include "RenderCommon/RenderResources.h"

#define DX12_FORMAT(format) ConvertToDx12Format(format)
static DXGI_FORMAT ConvertToDx12Format(render::eFormat format)
{
	using namespace render;
	switch (format)
	{
	case eFormat::UNKNOWN      : return DXGI_FORMAT_UNKNOWN;

	case eFormat::RGBA32_FLOAT : return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case eFormat::RGBA32_UINT  : return DXGI_FORMAT_R32G32B32A32_UINT;
	case eFormat::RGBA32_SINT  : return DXGI_FORMAT_R32G32B32A32_SINT;
	case eFormat::RGB32_FLOAT  : return DXGI_FORMAT_R32G32B32_FLOAT;
	case eFormat::RGB32_UINT   : return DXGI_FORMAT_R32G32B32_UINT;
	case eFormat::RGB32_SINT   : return DXGI_FORMAT_R32G32B32_SINT;
	case eFormat::RG32_FLOAT   : return DXGI_FORMAT_R32G32_FLOAT;
	case eFormat::RG32_UINT    : return DXGI_FORMAT_R32G32_UINT;
	case eFormat::RG32_SINT    : return DXGI_FORMAT_R32G32_SINT;
	case eFormat::R32_FLOAT    : return DXGI_FORMAT_R32_FLOAT;
	case eFormat::R32_UINT     : return DXGI_FORMAT_R32_UINT;
	case eFormat::R32_SINT     : return DXGI_FORMAT_R32_SINT;

	case eFormat::RGBA16_FLOAT : return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case eFormat::RGBA16_UNORM : return DXGI_FORMAT_R16G16B16A16_UNORM;
	case eFormat::RGBA16_UINT  : return DXGI_FORMAT_R16G16B16A16_UINT;
	case eFormat::RGBA16_SNORM : return DXGI_FORMAT_R16G16B16A16_SNORM;
	case eFormat::RGBA16_SINT  : return DXGI_FORMAT_R16G16B16A16_SINT;
	case eFormat::RG16_FLOAT   : return DXGI_FORMAT_R16G16_FLOAT;
	case eFormat::RG16_UNORM   : return DXGI_FORMAT_R16G16_UNORM;
	case eFormat::RG16_SNORM   : return DXGI_FORMAT_R16G16_SNORM;
	case eFormat::RG16_UINT    : return DXGI_FORMAT_R16G16_UINT;
	case eFormat::RG16_SINT    : return DXGI_FORMAT_R16G16_SINT;
	case eFormat::R16_FLOAT    : return DXGI_FORMAT_R16_FLOAT;
	case eFormat::R16_UNORM    : return DXGI_FORMAT_R16_UNORM;
	case eFormat::R16_SNORM    : return DXGI_FORMAT_R16_SNORM;
	case eFormat::R16_UINT     : return DXGI_FORMAT_R16_UINT;
	case eFormat::R16_SINT     : return DXGI_FORMAT_R16_SINT;

	case eFormat::RGBA8_UNORM : return DXGI_FORMAT_R8G8B8A8_UNORM;
	case eFormat::RGBA8_SNORM : return DXGI_FORMAT_R8G8B8A8_SNORM;
	case eFormat::RGBA8_UINT  : return DXGI_FORMAT_R8G8B8A8_UINT;
	case eFormat::RGBA8_SINT  : return DXGI_FORMAT_R8G8B8A8_SINT;
	case eFormat::RGBA8_SRGB  : return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case eFormat::RG8_UNORM   : return DXGI_FORMAT_R8G8_UNORM;
	case eFormat::RG8_SNORM   : return DXGI_FORMAT_R8G8_SNORM;
	case eFormat::RG8_UINT    : return DXGI_FORMAT_R8G8_UINT;
	case eFormat::RG8_SINT    : return DXGI_FORMAT_R8G8_SINT;
	case eFormat::R8_UNORM    : return DXGI_FORMAT_R8_UNORM;
	case eFormat::R8_SNORM    : return DXGI_FORMAT_R8_SNORM;
	case eFormat::R8_UINT     : return DXGI_FORMAT_R8_UINT;
	case eFormat::R8_SINT     : return DXGI_FORMAT_R8_SINT;
	case eFormat::A8_UNORM    : return DXGI_FORMAT_A8_UNORM;

	case eFormat::RG11B10_UFLOAT : return DXGI_FORMAT_R11G11B10_FLOAT;

	case eFormat::D32_FLOAT         : return DXGI_FORMAT_D32_FLOAT;
	case eFormat::D24_UNORM_S8_UINT : return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case eFormat::D16_UNORM         : return DXGI_FORMAT_D16_UNORM;

	default:
		assert(false && "Invalid format!"); break;
	}

	return DXGI_FORMAT_UNKNOWN;
}

#define DX12_RESOURCE_STATE(state, stage) ConvertToDx12ResourceState(state, stage)
static D3D12_RESOURCE_STATES ConvertToDx12ResourceState(render::eTextureLayout layout, render::eShaderStage stage)
{
	using namespace render;
	switch (layout)
	{
	case eTextureLayout::Undefined             : return D3D12_RESOURCE_STATE_COMMON;
	case eTextureLayout::General               : return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	case eTextureLayout::ColorAttachment       : return D3D12_RESOURCE_STATE_RENDER_TARGET;
	case eTextureLayout::DepthStencilAttachment: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
	case eTextureLayout::DepthStencilReadOnly  : return D3D12_RESOURCE_STATE_DEPTH_READ;
	case eTextureLayout::ShaderReadOnly        : return stage == eShaderStage::Compute ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	case eTextureLayout::TransferSource        : return D3D12_RESOURCE_STATE_COPY_SOURCE;
	case eTextureLayout::TransferDest          : return D3D12_RESOURCE_STATE_COPY_DEST;
	case eTextureLayout::Present               : return D3D12_RESOURCE_STATE_PRESENT;

	default:
		assert(false && "Invalid image layout!"); break;
	}

	return D3D12_RESOURCE_STATE_COMMON;
}

#define DX12_COMPAREOP(op) ConvertToDx12CompareOp(op)
static D3D12_COMPARISON_FUNC ConvertToDx12CompareOp(render::eCompareOp op)
{
	using namespace render;
	switch (op)
	{
	case eCompareOp::Never        : return D3D12_COMPARISON_FUNC_NEVER;
	case eCompareOp::Less         : return D3D12_COMPARISON_FUNC_LESS;
	case eCompareOp::Equal        : return D3D12_COMPARISON_FUNC_EQUAL;
	case eCompareOp::LessEqual    : return D3D12_COMPARISON_FUNC_LESS_EQUAL;
	case eCompareOp::Greater      : return D3D12_COMPARISON_FUNC_GREATER;
	case eCompareOp::NotEqual     : return D3D12_COMPARISON_FUNC_NOT_EQUAL;
	case eCompareOp::GreaterEqual : return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
	case eCompareOp::Always       : return D3D12_COMPARISON_FUNC_ALWAYS;

	default:
		assert(false && "Invalid compare op!"); break;
	}

	return D3D12_COMPARISON_FUNC_NONE;
}