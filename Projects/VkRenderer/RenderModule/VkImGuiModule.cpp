#include "RendererPch.h"
#include "VkImGuiModule.h"
#include "RenderDevice/VkSwapChain.h"
#include "RenderDevice/VkCommandQueue.h"
#include "RenderDevice/VkCommandBuffer.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

namespace vk
{

u32 g_minImageCount = 2;

ImGuiModule::ImGuiModule(RenderContext& context, vk::SwapChain& swapChain, ImGuiContext* pImGuiContext)
	: Super(context)
{
	assert(pImGuiContext);
	ImGui::SetCurrentContext(pImGuiContext);

	VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE };
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	descriptorPoolInfo.maxSets = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
	descriptorPoolInfo.poolSizeCount = 1;
	descriptorPoolInfo.pPoolSizes = &poolSize;
	VK_CHECK(vkCreateDescriptorPool(context.vkDevice(), &descriptorPoolInfo, nullptr, &m_vkImGuiPool));

	ImGui_ImplVulkan_InitInfo imguiInfo = {};
	imguiInfo.ApiVersion = VK_API_VERSION_1_3;
	imguiInfo.Instance = context.vkInstance();
	imguiInfo.PhysicalDevice = context.vkPhysicalDevice();
	imguiInfo.Device = context.vkDevice();
	imguiInfo.QueueFamily = context.GraphicsQueue().Index();
	imguiInfo.Queue = context.GraphicsQueue().vkQueue();
	imguiInfo.DescriptorPool = m_vkImGuiPool;
	imguiInfo.RenderPass = context.vkMainRenderPass();
	imguiInfo.MinImageCount = g_minImageCount;
	imguiInfo.ImageCount = swapChain.Capabilities().maxImageCount; // set larger count to prevent validation error from CreateOrResizeBuffer(..) - line.523 in imgui_impl_vulkan.cpp
	imguiInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	imguiInfo.CheckVkResultFn = ThrowIfFailed;
	ImGui_ImplVulkan_Init(&imguiInfo);

	ImGui_ImplVulkan_CreateFontsTexture();
}

ImGuiModule::~ImGuiModule()
{
	ImGui_ImplVulkan_Shutdown();
	vkDestroyDescriptorPool(m_renderContext.vkDevice(), m_vkImGuiPool, nullptr);
}

void ImGuiModule::Apply(CommandBuffer& cmdBuffer)
{
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuffer.vkCommandBuffer());

	cmdBuffer.EndRenderPass();
}

} // namespace vk