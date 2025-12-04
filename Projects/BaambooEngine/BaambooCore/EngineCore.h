#pragma once
#include "Window.h"
#include "RenderCommon/RendererAPI.h"

#include <stdlib.h>
#include <shlwapi.h>
#include <corecrt_wstdio.h>
#include <string>
#include <filesystem>

bool typedef (__stdcall* CreateInstance)(baamboo::Window* pWindow, struct ImGuiContext* pImGuiContext, void* ppv);

inline bool LoadRenderer(eRendererAPI eApi, baamboo::Window* pWindow, struct ImGuiContext* pImGuiContext, OUT render::Renderer** ppRenderer)
{
	HMODULE hEngineDLL = nullptr;
	std::wstring enginePath;
#ifdef _DEBUG
	enginePath = L"Output/Binaries/Debug/";
#else
	enginePath = L"Output/Binaries/Release/";
#endif

// #if defined(_M_ARM64EC) || defined(_M_ARM64)
// #elif defined(_M_AMD64)
// #endif
	switch (eApi)
	{
	case eRendererAPI::D3D12:
		enginePath += L"windows/Dx12Renderer/Dx12Renderer.dll";
		break;
	case eRendererAPI::Vulkan:
		enginePath += L"windows/VkRenderer/VkRenderer.dll";
		break;

	default:
		return FALSE;
	}
	WCHAR wchErrTxt[256] = {};
	DWORD dwErrCode = 0;

	hEngineDLL = LoadLibrary(enginePath.c_str());
	if (!hEngineDLL)
	{ 
		dwErrCode = GetLastError();
		//fs::path absolutePath = fs::absolute(enginePath);
		swprintf_s(wchErrTxt, L"Fail to LoadLibrary(%s) - Error Code: %u", enginePath.c_str(), dwErrCode);
		MessageBox(pWindow->WinHandle(), wchErrTxt, L"Error", MB_OK);
		__debugbreak();
	}

	CreateInstance fnCreateInstance = (CreateInstance)GetProcAddress(hEngineDLL, "DllCreateInstance");
	return fnCreateInstance(pWindow, pImGuiContext, ppRenderer);
}