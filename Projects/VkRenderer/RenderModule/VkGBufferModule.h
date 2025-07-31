#pragma once
#include "VkRenderModule.h"

namespace vk
{

class GBufferModule : public RenderModule
{
using Super = RenderModule;
public:
	GBufferModule(RenderDevice& device);
	virtual ~GBufferModule();

	virtual void Apply(CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	RenderTarget*     m_pRenderTarget = nullptr;
	GraphicsPipeline* m_pGraphicsPipeline = nullptr;
};

} // namespace vk