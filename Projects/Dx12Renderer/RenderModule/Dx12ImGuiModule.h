#pragma once
#include "RenderResource/Dx12RenderTarget.h"

struct ImGuiContext;

namespace dx12
{

class Dx12Texture;

class ImGuiModule
{
public:
	ImGuiModule(Dx12RenderDevice& rd, ImGuiContext* pImGuiContext);
	virtual ~ImGuiModule();

	void Apply(Dx12CommandContext& context, Arc< Dx12Texture > pColor);

private:
	Dx12RenderDevice& m_RenderDevice;

	ID3D12DescriptorHeap* m_d3d12SrvDescHeap = nullptr;
};

} // namespace dx12