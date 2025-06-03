#pragma once
#include "VkRenderModule.h"

namespace vk
{

class RenderTarget;
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
	RenderTarget*     m_pRenderTarget = nullptr;
	GraphicsPipeline* m_pGraphicsPipeline = nullptr;
};

} // namespace vk