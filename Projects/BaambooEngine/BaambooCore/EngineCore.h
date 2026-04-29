#pragma once
#include "Window.h"
#include "RenderCommon/RendererAPI.h"

#include <stdlib.h>
#include <shlwapi.h>
#include <corecrt_wstdio.h>
#include <string>
#include <filesystem>

bool typedef (__stdcall* CreateInstance)(baamboo::Window* pWindow, const render::DeviceSettings& ds, struct ImGuiContext* pImGuiContext, void* ppv);

inline bool LoadRenderer(eRendererAPI eApi, baamboo::Window* pWindow, const render::DeviceSettings& ds, struct ImGuiContext* pImGuiContext, OUT render::Renderer** ppRenderer)
{
	HMODULE hEngineDLL = nullptr;

	fs::path rendererDir;
	std::wstring rendererDLL;
#ifdef _DEBUG
	rendererDir = L"Output/Binaries/Debug/";
#else
	rendererDir = L"Output/Binaries/Release/";
#endif

// #if defined(_M_ARM64EC) || defined(_M_ARM64)
// #elif defined(_M_AMD64)
// #endif
	switch (eApi)
	{
	case eRendererAPI::D3D12:
		rendererDir += L"windows/Dx12Renderer/";
		rendererDLL = L"Dx12Renderer.dll";
		break;
	case eRendererAPI::Vulkan:
		rendererDir += L"windows/VkRenderer/";
		rendererDLL = L"VkRenderer.dll";
		break;

	default:
		return FALSE;
	}
	WCHAR wchErrTxt[256] = {};
	DWORD dwErrCode = 0;

	//hEngineDLL = LoadLibrary(enginePath.c_str());
	SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

	DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(fs::absolute(rendererDir).c_str());
	hEngineDLL = LoadLibraryExW(rendererDLL.c_str(), NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
	if (!hEngineDLL)
	{ 
		dwErrCode = GetLastError();
		//fs::path absolutePath = fs::absolute(enginePath);
		swprintf_s(wchErrTxt, L"Fail to LoadLibrary(%s) - Error Code: %u", rendererDir.c_str(), dwErrCode);
		MessageBox(pWindow->WinHandle(), wchErrTxt, L"Error", MB_OK);
		__debugbreak();
	}

	RemoveDllDirectory(cookie);

	CreateInstance fnCreateInstance = (CreateInstance)GetProcAddress(hEngineDLL, "DllCreateInstance");
	return fnCreateInstance(pWindow, ds, pImGuiContext, ppRenderer);
}