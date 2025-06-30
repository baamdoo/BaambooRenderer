#include "RendererPch.h"
#include "VkImGuiModule.h"
#include "RenderDevice/VkSwapChain.h"
#include "RenderDevice/VkCommandQueue.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderResource/VkSceneResource.h"

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
	descriptorPoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	descriptorPoolInfo.maxSets       = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
	descriptorPoolInfo.poolSizeCount = 1;
	descriptorPoolInfo.pPoolSizes    = &poolSize;
	VK_CHECK(vkCreateDescriptorPool(m_RenderDevice.vkDevice(), &descriptorPoolInfo, nullptr, &m_vkImGuiPool));

	ImGui_ImplVulkan_InitInfo imguiInfo = {};
	imguiInfo.ApiVersion                = VK_API_VERSION_1_3;
	imguiInfo.Instance                  = m_RenderDevice.vkInstance();
	imguiInfo.PhysicalDevice            = m_RenderDevice.vkPhysicalDevice();
	imguiInfo.Device                    = m_RenderDevice.vkDevice();
	imguiInfo.QueueFamily               = m_RenderDevice.GraphicsQueue().Index();
	imguiInfo.Queue                     = m_RenderDevice.GraphicsQueue().vkQueue();
	imguiInfo.DescriptorPool            = m_vkImGuiPool;
	//imguiInfo.RenderPass                = m_RenderDevice.vkMainRenderPass();
	imguiInfo.MinImageCount             = g_minImageCount;
	imguiInfo.ImageCount                = swapChain.Capabilities().maxImageCount; // set larger count to prevent validation error from CreateOrResizeBuffer(..) - line.523 in imgui_impl_vulkan.cpp
	imguiInfo.MSAASamples               = VK_SAMPLE_COUNT_1_BIT;
	imguiInfo.CheckVkResultFn           = ThrowIfFailed;

	auto imageFormat = swapChain.ImageFormat();
	imguiInfo.UseDynamicRendering                                 = true;
	imguiInfo.PipelineRenderingCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	imguiInfo.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
	imguiInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &imageFormat;
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
	context.TransitionImageLayout(
		g_FrameData.pColor.lock(),
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
		VK_IMAGE_ASPECT_COLOR_BIT
	);

	VkRenderingAttachmentInfo colorAttachment = 
	{
		.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView   = g_FrameData.pColor->vkView(),
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp     = VK_ATTACHMENT_STORE_OP_STORE
	};
	VkRenderingInfo renderInfo = 
	{
		.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea           = { { 0, 0 }, { g_FrameData.pColor->Desc().extent.width, g_FrameData.pColor->Desc().extent.height}},
		.layerCount           = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments    = &colorAttachment
	};
	
	context.BeginRendering(renderInfo);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), context.vkCommandBuffer());
	context.EndRendering();
}

} // namespace vk