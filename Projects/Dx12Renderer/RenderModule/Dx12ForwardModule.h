#pragma once
#include "Dx12RenderModule.h"
#include "RenderResource/Dx12RenderTarget.h"

namespace dx12
{

class RootSignature;
class GraphicsPipeline;

class ForwardModule : public RenderModule
{
	using Super = RenderModule;
public:
	ForwardModule(RenderDevice& device);
	virtual ~ForwardModule();

	virtual void Apply(CommandContext& context) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	RenderTarget      m_RenderTarget;
	RootSignature*    m_pRootSignature = nullptr;
	GraphicsPipeline* m_pGraphicsPipeline = nullptr;
};

} // namespace dx12