#pragma once
#include "VkRenderModule.h"

namespace vk
{

class ForwardModule : public RenderModule
{
using Super = RenderModule;
public:
	ForwardModule(RenderDevice& device);
	virtual ~ForwardModule();

	virtual void Apply(CommandContext& context) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
};

} // namespace vk