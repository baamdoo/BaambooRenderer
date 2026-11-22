#pragma once
#include "Window.h"
#include "RenderCommon/RendererAPI.h"
#include "Utils/ModulePath.hpp"

#include <stdlib.h>
#include <shlwapi.h>
#include <corecrt_wstdio.h>
#include <string>
#include <filesystem>

bool typedef (__stdcall* CreateInstance)(baamboo::Window* pWindow, const render::DeviceSettings& ds, struct ImGuiContext* pImGuiContext, void* ppv);

inline bool LoadRenderer(eRendererAPI eApi, baamboo::Window* pWindow, const render::DeviceSettings& ds, struct ImGuiContext* pImGuiContext, OUT render::Renderer** ppRenderer)
{
	HMODULE hEngineDLL = nullptr;

	std::wstring rendererDLL;
	switch (eApi)
	{
	case eRendererAPI::D3D12:
		rendererDLL = L"Dx12Renderer.dll";
		break;
	case eRendererAPI::Vulkan:
		rendererDLL = L"VkRenderer.dll";
		break;

	default:
		return FALSE;
	}
	WCHAR wchErrTxt[512] = {};
	DWORD dwErrCode = 0;

	const fs::path rendererPath = ModulePath::ThisModuleDirectory() / rendererDLL;
	hEngineDLL = LoadLibraryExW(rendererPath.c_str(), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!hEngineDLL)
	{
		dwErrCode = GetLastError();
		swprintf_s(wchErrTxt, L"Fail to LoadLibrary(%s) - Error Code: %u", rendererPath.c_str(), dwErrCode);
		MessageBox(pWindow->WinHandle(), wchErrTxt, L"Error", MB_OK);
		__debugbreak();
	}
	//system("PAUSE");

	CreateInstance fnCreateInstance = (CreateInstance)GetProcAddress(hEngineDLL, "DllCreateInstance");
	return fnCreateInstance(pWindow, ds, pImGuiContext, ppRenderer);
}