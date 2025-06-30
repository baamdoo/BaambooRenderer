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

	virtual void Apply(CommandContext& context) override;

private:
};

} // namespace dx12