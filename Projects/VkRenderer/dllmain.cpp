#include "RendererPch.h"
#include "VkRenderer.h"

#include <Windows.h>

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

bool DllCreateInstance(baamboo::Window* pWindow, ImGuiContext* pImGuiContext, OUT void** ppv)
{
	render::Renderer* pRenderer = new vk::VkRenderer(pWindow, pImGuiContext);
	if (!pRenderer)
		return false;

	*ppv = pRenderer;
	return true;
}