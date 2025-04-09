#pragma once
#include <cstdint>
#include <codecvt>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <comdef.h>


//-------------------------------------------------------------------------
// Header
//-------------------------------------------------------------------------
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3d11on12.h>
#include <d3dcompiler.h>
#include <dwrite.h>
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


//-------------------------------------------------------------------------
// Predefined
//-------------------------------------------------------------------------
constexpr u32 NUM_FRAMES = 3u;
constexpr u32 NUM_SAMPLING = 1u;
constexpr u32 NUM_RESOURCE_DESCRIPTOR_TYPE = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER + 1;
constexpr u32 MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = { 1024, 32, 256, 8 };


//-------------------------------------------------------------------------
// Render Context
//-------------------------------------------------------------------------
#include "RenderDevice/Dx12RenderContext.h"