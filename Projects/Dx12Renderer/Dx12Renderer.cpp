#include "RendererPch.h"
#include "Dx12Renderer.h"
#include "RenderDevice/Dx12SwapChain.h"
#include "RenderDevice/Dx12DescriptorPool.h"
#include "RenderDevice/Dx12CommandQueue.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderModule/Dx12GBufferModule.h"
#include "RenderModule/Dx12LightingModule.h"
#include "RenderModule/Dx12ImGuiModule.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12SceneResource.h"
#include "SceneRenderView.h"

#include <shlobj.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx12.h>

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

	m_pRenderDevice = new RenderDevice(false);
	m_pSwapChain    = new SwapChain(*m_pRenderDevice, *pWindow);

	g_FrameData.pSceneResource = new SceneResource(*m_pRenderDevice);

	m_pRenderModules.push_back(new GBufferModule(*m_pRenderDevice));
	m_pRenderModules.push_back(new LightingModule(*m_pRenderDevice));
	m_pRenderModules.push_back(new ImGuiModule(*m_pRenderDevice, pImGuiContext));

	printf("D3D12Renderer constructed!\n");
}

Renderer::~Renderer()
{
	m_pRenderDevice->Flush();

	RELEASE(g_FrameData.pSceneResource);
	for (auto& pModule : m_pRenderModules)
		RELEASE(pModule);

	RELEASE(m_pSwapChain);
	RELEASE(m_pRenderDevice);

	CoUninitialize();
	printf("D3D12Renderer destructed!\n");
}

void Renderer::NewFrame()
{
	ImGui_ImplDX12_NewFrame();
}

void Renderer::Render(SceneRenderView&& renderView)
{
	assert(g_FrameData.pSceneResource);
	g_FrameData.pSceneResource->UpdateSceneResources(renderView);

	auto& context = BeginFrame();
	{
		CameraData camera   = {};
		camera.mView        = renderView.camera.mView;
		camera.mProj        = renderView.camera.mProj;
		camera.mViewProj    = camera.mProj * camera.mView;
		camera.mViewProjInv = glm::inverse(camera.mViewProj);
		camera.position     = renderView.camera.pos;
		g_FrameData.camera  = std::move(camera);

		for (auto pModule : m_pRenderModules)
			pModule->Apply(context);
	}
	EndFrame(context);
}

void Renderer::OnWindowResized(i32 width, i32 height)
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

	for (auto pModule : m_pRenderModules)
		pModule->Resize(width, height);
}

void Renderer::SetRendererType(eRendererType type)
{
	m_type = type;
}

CommandContext& Renderer::BeginFrame()
{
	auto& cmdList = m_pRenderDevice->BeginCommand(D3D12_COMMAND_LIST_TYPE_DIRECT);
	return cmdList;
}

void Renderer::EndFrame(CommandContext& context)
{
	auto frameIndex    = m_pRenderDevice->FrameIndex();
	auto& commandQueue = m_pRenderDevice->GraphicsQueue();

	auto pBackImage = m_pSwapChain->GetBackImage();
	if constexpr (NUM_SAMPLING > 1)
	{
		if (g_FrameData.pColor.valid())
		{
			context.ResolveSubresource(pBackImage, g_FrameData.pColor.lock());
		}
	}
	else
	{
		if (g_FrameData.pColor.valid())
		{
			context.CopyTexture(pBackImage, g_FrameData.pColor.lock());
		}
	}
	context.TransitionBarrier(pBackImage, D3D12_RESOURCE_STATE_PRESENT);
	context.Close();

	auto fenceValue = commandQueue.ExecuteCommandList(&context);
	m_FrameFenceValue[frameIndex] = fenceValue;

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
	commandQueue.WaitForFenceValue(m_FrameFenceValue[nextFrameIndex]);
}

} // namespace dx12
