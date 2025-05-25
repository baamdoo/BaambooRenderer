#pragma once
#include "VkRenderModule.h"

struct ImGuiContext;

namespace vk
{

class SwapChain;

class ImGuiModule : public RenderModule
{
using Super = RenderModule;
public:
	ImGuiModule(RenderContext& context, vk::SwapChain& swapChain, ImGuiContext* pImGuiContext);
	~ImGuiModule();

	void Apply(CommandBuffer& cmdBuffer) override;

private:
	VkDescriptorPool m_vkImGuiPool = nullptr;
};

} // namespace vk