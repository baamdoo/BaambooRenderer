#pragma once
struct ImGuiContext;

namespace vk
{

class SwapChain;
class VkCommandContext;
class VulkanTexture;

class ImGuiModule
{
public:
	ImGuiModule(VkRenderDevice& rd, vk::SwapChain& swapChain, ImGuiContext* pImGuiContext);
	~ImGuiModule();

	void Apply(VkCommandContext& context, Arc< VulkanTexture > pColor);

private:
	VkRenderDevice& m_RenderDevice;

	VkDescriptorPool m_vkImGuiPool = VK_NULL_HANDLE;
};

} // namespace vk