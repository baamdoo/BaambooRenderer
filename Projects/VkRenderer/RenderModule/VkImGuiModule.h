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
	ImGuiModule(RenderDevice& device, vk::SwapChain& swapChain, ImGuiContext* pImGuiContext);
	~ImGuiModule();

	void Apply(CommandContext& context) override;

private:
	VkDescriptorPool m_vkImGuiPool = nullptr;
};

} // namespace vk