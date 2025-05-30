#include "RendererPch.h"
#include "Dx12Renderer.h"
#include "RenderDevice/Dx12SwapChain.h"
#include "RenderDevice/Dx12DescriptorPool.h"
#include "RenderDevice/Dx12CommandQueue.h"
#include "RenderDevice/Dx12CommandList.h"
#include "RenderModule/Dx12ForwardModule.h"
#include "RenderModule/Dx12ImGuiModule.h"
#include "RenderResource/Dx12SceneResource.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx12.h>
#include <Scene/SceneRenderView.h>

namespace dx12
{

Renderer::Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext)
{
	DX_CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

	m_pRenderContext = new RenderContext(true);
	m_pSwapChain = new SwapChain(*m_pRenderContext, *pWindow);

	m_pRenderModules.push_back(new ForwardModule(*m_pRenderContext));
	m_pRenderModules.push_back(new ImGuiModule(*m_pRenderContext, pImGuiContext));

	printf("D3D12Renderer constructed!\n");
}

Renderer::~Renderer()
{
	m_pRenderContext->Flush();

	for (auto pModule : m_pRenderModules)
		RELEASE(pModule);

	RELEASE(m_pSwapChain);
	RELEASE(m_pRenderContext);

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

	auto& cmdList = BeginFrame();
	{
		CameraData camera = {};
		camera.mView = renderView.camera.mView;
		camera.mProj = renderView.camera.mProj;
		camera.mViewProj = camera.mProj * camera.mView;
		camera.position = renderView.camera.pos;
		g_FrameData.camera = std::move(camera);

		for (auto pModule : m_pRenderModules)
			pModule->Apply(cmdList);
	}
	EndFrame(cmdList);
}

void Renderer::OnWindowResized(i32 width, i32 height)
{
	if (width == 0 || height == 0)
		return;

	if (!m_pSwapChain)
		return;

	if (m_pRenderContext->WindowWidth() == static_cast<u32>(width) && m_pRenderContext->WindowHeight() == static_cast<u32>(height))
		return;

	m_pRenderContext->Flush();
	m_pRenderContext->SetWindowWidth(width);
	m_pRenderContext->SetWindowHeight(height);

	m_pSwapChain->ResizeViewport(width, height);

	for (auto pModule : m_pRenderModules)
		pModule->Resize(width, height);
}

void Renderer::SetRendererType(eRendererType type)
{
	m_type = type;
}

CommandList& Renderer::BeginFrame()
{
	auto& cmdList = m_pRenderContext->AllocateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT);
	return cmdList;
}

void Renderer::EndFrame(CommandList& cmdList)
{
	auto& rm = m_pRenderContext->GetResourceManager();

	auto contextIndex = m_pRenderContext->ContextIndex();
	auto& commandQueue = m_pRenderContext->GetCommandQueue();

	auto backBuffer = m_pSwapChain->GetImageToPresent();
	if constexpr (NUM_SAMPLING > 1)
	{
		cmdList.ResolveSubresource(rm.Get(backBuffer), g_FrameData.pColor);
	}
	else
	{
		cmdList.CopyTexture(rm.Get(backBuffer), g_FrameData.pColor);
	}
	cmdList.TransitionBarrier(rm.Get(backBuffer), D3D12_RESOURCE_STATE_PRESENT);
	cmdList.Close();

	auto fenceValue = commandQueue.ExecuteCommandList(&cmdList);
	m_ContextFenceValue[contextIndex] = fenceValue;

	m_pSwapChain->Present();

	UINT nextContextIndex = m_pRenderContext->Swap();
	commandQueue.WaitForFenceValue(m_ContextFenceValue[nextContextIndex]);
}

} // namespace dx12
