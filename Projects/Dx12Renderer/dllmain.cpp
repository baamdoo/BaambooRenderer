#include "RendererPch.h"
#include "Dx12Renderer.h"

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
	)
{
	UNUSED(hModule);
	UNUSED(lpReserved);

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
#if defined(_DEBUG) || defined(DEBUG)
		int	flags = _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF;
		_CrtSetDbgFlag(flags);
#endif
	}
	break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
	{
#ifdef _DEBUG
		_ASSERT(_CrtCheckMemory());
		atexit(&ReportLiveObjects);
#endif
	}
	break;
	}
	return TRUE;
}

namespace baamboo
{
	class Window;
}

bool DllCreateInstance(baamboo::Window* pWindow, ImGuiContext* pImGuiContext, void** ppv)
{
	render::Renderer* pEngine = new dx12::Renderer(pWindow, pImGuiContext);
	if (!pEngine)
		return false;

	*ppv = pEngine;
	return true;
}