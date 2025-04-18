#include "RendererPch.h"
#include "Dx12Renderer.h"
#include "RenderDevice/Dx12RenderContext.h"
#include "RenderDevice/Dx12ResourceManager.h"
#include "RenderDevice/Dx12SwapChain.h"
#include "RenderDevice/Dx12DescriptorPool.h"
#include "RenderDevice/Dx12CommandQueue.h"
#include "RenderDevice/Dx12CommandList.h"
#include "RenderModule/Dx12ForwardRenderModule.h"

#include <Scene/SceneRenderView.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx12.h>

namespace ImGui
{

ID3D12DescriptorHeap* g_d3d12SrvDescHeap = nullptr;

void InitUI(dx12::RenderContext& context, ImGuiContext* pImGuiContext)
{
	assert(pImGuiContext);
	ImGui::SetCurrentContext(pImGuiContext);

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = 64;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	DX_CHECK(context.GetD3D12Device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_d3d12SrvDescHeap)) != S_OK);

	ImGui_ImplDX12_InitInfo info = {};
	info.Device = context.GetD3D12Device();
	info.CommandQueue = context.GetCommandQueue().GetD3D12CommandQueue();
	info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	info.NumFramesInFlight = NUM_FRAMES;
	info.SrvDescriptorHeap = g_d3d12SrvDescHeap;
	info.LegacySingleSrvCpuDescriptor = g_d3d12SrvDescHeap->GetCPUDescriptorHandleForHeapStart();
	info.LegacySingleSrvGpuDescriptor = g_d3d12SrvDescHeap->GetGPUDescriptorHandleForHeapStart();
	ImGui_ImplDX12_Init(&info);
}

void DrawUI(dx12::CommandList& cmdList)
{
	cmdList.SetDescriptorHeaps({ g_d3d12SrvDescHeap });
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.GetD3D12CommandList());
}

void Destroy()
{
	ImGui_ImplDX12_Shutdown();
	g_d3d12SrvDescHeap->Release();
}

} // namespace ImGui

namespace dx12
{

Renderer::Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext)
{
	m_pRenderContext = new RenderContext();
	m_pSwapChain = new SwapChain(*m_pRenderContext, *pWindow);

	ImGui::InitUI(*m_pRenderContext, pImGuiContext);
	ForwardPass::Initialize(*m_pRenderContext);

	printf("D3D12Renderer constructed!\n");
}

Renderer::~Renderer()
{
	m_pRenderContext->Flush();

	ForwardPass::Destroy();
	ImGui::Destroy();

	RELEASE(m_pSwapChain);
	RELEASE(m_pRenderContext);
	printf("D3D12Renderer destructed!\n");
}

void Renderer::NewFrame()
{
	ImGui_ImplDX12_NewFrame();
}

float4 testColor{};
void Renderer::Render(const baamboo::SceneRenderView& renderView)
{
	auto& rm = m_pRenderContext->GetResourceManager();
	for (const auto& transform : renderView.transforms)
	{
		// update transform buffer
	}

	for (const auto& mesh : renderView.meshes)
	{
		// update vertex/index/textures
		// rm.LoadOrGet< Buffer >(mesh.geometry);
		// rm.LoadOrGet< Texture >(mesh.material.albedo);
	}

	for (const auto& camera : renderView.cameras)
	{
		// update camera buffer
		if (camera.id == 0)
			testColor = float4(camera.pos, 1.0f);
	}

	auto& cmdList = BeginFrame();
	RenderFrame(cmdList);
	EndFrame(cmdList);
}

void Renderer::OnWindowResized(i32 width, i32 height)
{
	if (width == 0 || height == 0)
		return;

	if (!m_pSwapChain)
		return;

	if (m_pRenderContext->ViewportWidth() == static_cast<u32>(width) && m_pRenderContext->ViewportHeight() == static_cast<u32>(height))
		return;

	m_pRenderContext->Flush();
	m_pRenderContext->SetViewportWidth(width);
	m_pRenderContext->SetViewportHeight(height);

	m_pSwapChain->ResizeViewport(width, height);

	ForwardPass::Resize(width, height);
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

void Renderer::RenderFrame(CommandList& cmdList)
{
	{
		ForwardPass::Apply(cmdList, testColor);
	}
	ImGui::DrawUI(cmdList);
}

void Renderer::EndFrame(CommandList& cmdList)
{
	auto& rm = m_pRenderContext->GetResourceManager();

	auto contextIndex = m_pRenderContext->ContextIndex();
	auto& commandQueue = m_pRenderContext->GetCommandQueue();

	auto pMainTarget = ForwardPass::GetRenderedTexture(eAttachmentPoint::Color0);
	auto backBuffer = m_pSwapChain->GetImageToPresent();
	if constexpr (NUM_SAMPLING > 1)
	{
		cmdList.ResolveSubresource(rm.Get(backBuffer), pMainTarget);
	}
	else
	{
		cmdList.CopyTexture(rm.Get(backBuffer), pMainTarget);
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
