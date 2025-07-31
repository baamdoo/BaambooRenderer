#pragma once
#include "Dx12RenderModule.h"
#include "RenderResource/Dx12RenderTarget.h"

namespace dx12
{

class ForwardModule : public RenderModule
{
using Super = RenderModule;
public:
	ForwardModule(RenderDevice& device);
	virtual ~ForwardModule();

	virtual void Apply(CommandContext& context, const SceneRenderView& renderView) override;

private:
	RootSignature*    m_pForwardRS = nullptr;
	GraphicsPipeline* m_pForwardPSO = nullptr;
};

} // namespace dx12