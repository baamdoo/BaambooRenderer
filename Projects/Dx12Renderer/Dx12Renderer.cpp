#include "RendererPch.h"
#include "Dx12Renderer.h"
#include "RenderDevice/Dx12SwapChain.h"
#include "RenderDevice/Dx12DescriptorPool.h"
#include "RenderDevice/Dx12CommandQueue.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderModule/Dx12ForwardModule.h"
#include "RenderModule/Dx12ImGuiModule.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12SceneResource.h"
#include "SceneRenderView.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx12.h>

namespace dx12
{

Renderer::Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext)
{
	DX_CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

	m_pRenderDevice = new RenderDevice(true);
	m_pSwapChain = new SwapChain(*m_pRenderDevice, *pWindow);

	g_FrameData.pSceneResource = new SceneResource(*m_pRenderDevice);

	m_pRenderModules.push_back(new ForwardModule(*m_pRenderDevice));
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
		CameraData camera = {};
		camera.mView = renderView.camera.mView;
		camera.mProj = renderView.camera.mProj;
		camera.mViewProj = camera.mProj * camera.mView;
		camera.position = renderView.camera.pos;
		g_FrameData.camera = std::move(camera);

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
	auto frameIndex = m_pRenderDevice->FrameIndex();
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

	m_pSwapChain->Present();

	UINT nextFrameIndex = m_pRenderDevice->Swap();
	commandQueue.WaitForFenceValue(m_FrameFenceValue[nextFrameIndex]);
}

} // namespace dx12
