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

	void Apply(CommandContext& context, const SceneRenderView& renderView) override;

private:
	RenderTarget*    m_pRenderTarget = nullptr;
	VkDescriptorPool m_vkImGuiPool   = VK_NULL_HANDLE;
};

} // namespace vk