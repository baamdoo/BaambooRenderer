#include "RendererPch.h"
#include "VkImGuiModule.h"
#include "RenderDevice/VkSwapChain.h"
#include "RenderDevice/VkCommandQueue.h"
#include "RenderDevice/VkCommandContext.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

namespace vk
{

u32 g_minImageCount = 2;

ImGuiModule::ImGuiModule(RenderDevice& device, vk::SwapChain& swapChain, ImGuiContext* pImGuiContext)
	: Super(device)
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
	VK_CHECK(vkCreateDescriptorPool(m_RenderDevice.vkDevice(), &descriptorPoolInfo, nullptr, &m_vkImGuiPool));

	ImGui_ImplVulkan_InitInfo imguiInfo = {};
	imguiInfo.ApiVersion = VK_API_VERSION_1_3;
	imguiInfo.Instance = m_RenderDevice.vkInstance();
	imguiInfo.PhysicalDevice = m_RenderDevice.vkPhysicalDevice();
	imguiInfo.Device = m_RenderDevice.vkDevice();
	imguiInfo.QueueFamily = m_RenderDevice.GraphicsQueue().Index();
	imguiInfo.Queue = m_RenderDevice.GraphicsQueue().vkQueue();
	imguiInfo.DescriptorPool = m_vkImGuiPool;
	imguiInfo.RenderPass = m_RenderDevice.vkMainRenderPass();
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
	vkDestroyDescriptorPool(m_RenderDevice.vkDevice(), m_vkImGuiPool, nullptr);
}

void ImGuiModule::Apply(CommandContext& context)
{
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), context.vkCommandBuffer());

	context.EndRenderPass();
}

} // namespace vk