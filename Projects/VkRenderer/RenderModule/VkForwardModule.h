#pragma once
#include "VkRenderModule.h"

namespace vk
{

class CommandBuffer;
class RenderTarget;
class GraphicsPipeline;

class ForwardModule : public RenderModule
{
using Super = RenderModule;
public:
	ForwardModule(RenderContext& context);
	virtual ~ForwardModule();

	virtual void Apply(CommandBuffer& cmdBuffer) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	RenderTarget*     m_pRenderTarget = nullptr;
	GraphicsPipeline* m_pGraphicsPipeline = nullptr;
};

} // namespace vk