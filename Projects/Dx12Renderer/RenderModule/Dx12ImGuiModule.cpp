#include "RendererPch.h"
#include "Dx12ImGuiModule.h"
#include "RenderDevice/Dx12CommandList.h"
#include "RenderDevice/Dx12CommandQueue.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx12.h>

namespace dx12
{

ImGuiModule::ImGuiModule(RenderContext& context, ImGuiContext* pImGuiContext)
	: Super(context)
{
	assert(pImGuiContext);
	ImGui::SetCurrentContext(pImGuiContext);

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = 64;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	DX_CHECK(context.GetD3D12Device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_d3d12SrvDescHeap)) != S_OK);

	ImGui_ImplDX12_InitInfo info = {};
	info.Device = context.GetD3D12Device();
	info.CommandQueue = context.GetCommandQueue().GetD3D12CommandQueue();
	info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	info.NumFramesInFlight = NUM_FRAMES_IN_FLIGHT;
	info.SrvDescriptorHeap = m_d3d12SrvDescHeap;
	info.LegacySingleSrvCpuDescriptor = m_d3d12SrvDescHeap->GetCPUDescriptorHandleForHeapStart();
	info.LegacySingleSrvGpuDescriptor = m_d3d12SrvDescHeap->GetGPUDescriptorHandleForHeapStart();
	ImGui_ImplDX12_Init(&info);
}

ImGuiModule::~ImGuiModule()
{
	ImGui_ImplDX12_Shutdown();
	COM_RELEASE(m_d3d12SrvDescHeap);
}

void ImGuiModule::Apply(CommandList& cmdList)
{
	cmdList.SetDescriptorHeaps({ m_d3d12SrvDescHeap });
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.GetD3D12CommandList());
}

} // namespace dx12