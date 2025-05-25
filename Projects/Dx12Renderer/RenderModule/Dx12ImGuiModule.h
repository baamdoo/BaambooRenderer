#pragma once
#include "Dx12RenderModule.h"
#include "RenderResource/Dx12RenderTarget.h"

struct ImGuiContext;

namespace dx12
{

class RootSignature;
class GraphicsPipeline;

class ImGuiModule : public RenderModule
{
	using Super = RenderModule;
public:
	ImGuiModule(RenderContext& context, ImGuiContext* pImGuiContext);
	virtual ~ImGuiModule();

	virtual void Apply(CommandList& cmdBuffer) override;

private:
	RenderTarget      m_RenderTarget;
	RootSignature*    m_pRootSignature = nullptr;
	GraphicsPipeline* m_pGraphicsPipeline = nullptr;

	ID3D12DescriptorHeap* m_d3d12SrvDescHeap = nullptr;
};

} // namespace dx12