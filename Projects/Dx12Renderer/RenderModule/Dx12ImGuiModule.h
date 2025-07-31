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
	ImGuiModule(RenderDevice& device, ImGuiContext* pImGuiContext);
	virtual ~ImGuiModule();

	virtual void Apply(CommandContext& context, const SceneRenderView& renderView) override;

private:
	RenderTarget m_RenderTarget;

	ID3D12DescriptorHeap* m_d3d12SrvDescHeap = nullptr;
};

} // namespace dx12