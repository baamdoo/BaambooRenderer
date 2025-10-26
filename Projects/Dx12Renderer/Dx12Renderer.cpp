#include "RendererPch.h"
#include "Dx12Renderer.h"
#include "RenderDevice/Dx12SwapChain.h"
#include "RenderDevice/Dx12DescriptorPool.h"
#include "RenderDevice/Dx12CommandQueue.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderModule/Dx12ImGuiModule.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12SceneResource.h"
#include "SceneRenderView.h"

#include <shlobj.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx12.h>
#include <Utils/Math.hpp>

namespace dx12
{

// https://devblogs.microsoft.com/pix/taking-a-capture/
static std::wstring GetLatestWinPixGpuCapturerPath()
{
	LPWSTR programFilesPath = nullptr;
	SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

	std::filesystem::path pixInstallationPath = programFilesPath;
	pixInstallationPath /= "Microsoft PIX";

	std::wstring newestVersionFound;

	for (auto const& directory_entry : std::filesystem::directory_iterator(pixInstallationPath))
	{
		if (directory_entry.is_directory())
		{
			if (newestVersionFound.empty() || newestVersionFound < directory_entry.path().filename().c_str())
			{
				newestVersionFound = directory_entry.path().filename().c_str();
			}
		}
	}

	if (newestVersionFound.empty())
	{
		return L"";
	}

	return pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll";
}

Renderer::Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext)
{
	DX_CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
	//if (auto pixPath = GetLatestWinPixGpuCapturerPath(); !pixPath.empty())
	//{
	//	// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
	//	// This may happen if the application is launched through the PIX UI. 
	//	if (GetModuleHandle(L"WinPixGpuCapturer.dll") == 0)
	//	{
	//		LoadLibrary(pixPath.c_str());
	//	}
	//}
	//system("PAUSE");

	m_pRenderDevice = new Dx12RenderDevice(false);
	m_pSwapChain    = new Dx12SwapChain(*m_pRenderDevice, *pWindow);
	m_pImGuiModule  = MakeBox< ImGuiModule >(*m_pRenderDevice, pImGuiContext);

	printf("D3D12Renderer constructed!\n");
}

Renderer::~Renderer()
{
	WaitIdle();

	RELEASE(m_pSwapChain);
	RELEASE(m_pRenderDevice);

	CoUninitialize();
	printf("D3D12Renderer destructed!\n");
}

void Renderer::NewFrame()
{
	ImGui_ImplDX12_NewFrame();
}

Arc< render::CommandContext > Renderer::BeginFrame()
{
	return m_pRenderDevice->GraphicsQueue().Allocate();
}

void Renderer::EndFrame(Arc< render::CommandContext >&& pContext, Arc< render::Texture > pScene, bool bDrawUI)
{
	auto contextIndex = m_pRenderDevice->ContextIndex();

	auto& cmdQueue   = m_pRenderDevice->GraphicsQueue();
	auto  rhiContext = StaticCast<Dx12CommandContext>(pContext);
	assert(rhiContext);

	auto pColor = StaticCast<Dx12Texture>(pScene);
	assert(pColor);
	if (bDrawUI)
	{
		m_pImGuiModule->Apply(*rhiContext, pColor);
	}

	auto pBackImage = m_pSwapChain->GetBackImage();
	if constexpr (NUM_SAMPLING > 1)
	{
		rhiContext->ResolveSubresource(pBackImage.get(), pColor.get());
	}
	else
	{
		rhiContext->CopyTexture(pBackImage, pColor);
	}
	rhiContext->TransitionBarrier(pBackImage.get(), D3D12_RESOURCE_STATE_PRESENT);
	rhiContext->Close();

	auto fenceValue = cmdQueue.ExecuteCommandList(rhiContext);
	m_FrameFenceValue[contextIndex] = fenceValue;

	auto hr = m_pSwapChain->Present();
	if (DXGI_ERROR_DEVICE_REMOVED == hr)
	{
		/*ID3D12DeviceRemovedExtendedData2* pDred;
		m_pRenderDevice->GetD3D12Device()->QueryInterface(IID_PPV_ARGS(&pDred));
		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs;
		if (SUCCEEDED(pDred->GetAutoBreadcrumbsOutput1(&breadcrumbs)))
		{
			const D3D12_AUTO_BREADCRUMB_NODE1* pNode = breadcrumbs.pHeadAutoBreadcrumbNode;
			while (pNode)
			{
				UINT lastCompletedIndex = *(pNode->pLastBreadcrumbValue);
				D3D12_AUTO_BREADCRUMB_OP lastOp = pNode->pCommandHistory[lastCompletedIndex];

				if (lastCompletedIndex < pNode->BreadcrumbCount - 1)
				{
					D3D12_AUTO_BREADCRUMB_OP hangingOp = pNode->pCommandHistory[lastCompletedIndex + 1];

					const char* opName = "";
					switch (hangingOp)
					{
						case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT: opName = "EXECUTEINDIRECT"; break;
						case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:   opName = "DRAWINSTANCED";   break;
						case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:    opName = "COPYRESOURCE";    break;
						case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: opName = "RESOURCEBARRIER"; break;
						default: opName = "UNKNOWN_OP";
					}
				}
				pNode = pNode->pNext;
			}
		}*/

		__debugbreak();
	}

	UINT nextFrameIndex = m_pRenderDevice->Swap();
	cmdQueue.WaitForFenceValue(m_FrameFenceValue[nextFrameIndex]);
}

void Renderer::WaitIdle()
{
	m_pRenderDevice->Flush();
}

void Renderer::Resize(i32 width, i32 height)
{
	if (width == 0 || height == 0)
		return;

	if (!m_pSwapChain)
		return;

	if (m_pRenderDevice->WindowWidth() == static_cast<u32>(width) && m_pRenderDevice->WindowHeight() == static_cast<u32>(height))
		return;

	m_pRenderDevice->Flush();
	m_pRenderDevice->SetWindowWidth(width);
	m_pRenderDevice->SetWindowHeight(height);

	m_pSwapChain->ResizeViewport(width, height);
}

} // namespace dx12
