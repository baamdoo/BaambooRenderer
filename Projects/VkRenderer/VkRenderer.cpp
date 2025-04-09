#include "RendererPch.h"
#include "VkRenderer.h"
#include "RenderDevice/VkSwapChain.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkCommandQueue.h"
#include "RenderDevice/VkCommandBuffer.h"
#include "RenderResource/VkTexture.h"
#include "RenderModule/VkForwardRenderModule.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

namespace ImGui
{

u32 g_minImageCount = 2;
VkDescriptorPool g_vkSrvDescPool = nullptr;

void InitUI(vk::RenderContext& context, vk::SwapChain& swapChain, ImGuiContext* pImGuiContext)
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
	VK_CHECK(vkCreateDescriptorPool(context.vkDevice(), &descriptorPoolInfo, nullptr, &g_vkSrvDescPool));

	ImGui_ImplVulkan_InitInfo imguiInfo = {};
	imguiInfo.ApiVersion = VK_API_VERSION_1_3;
	imguiInfo.Instance = context.vkInstance();
	imguiInfo.PhysicalDevice = context.vkPhysicalDevice();
	imguiInfo.Device = context.vkDevice();
	imguiInfo.QueueFamily = context.GraphicsQueue().Index();
	imguiInfo.Queue = context.GraphicsQueue().vkQueue();
	imguiInfo.DescriptorPool = g_vkSrvDescPool;
	imguiInfo.RenderPass = context.vkMainRenderPass();
	imguiInfo.MinImageCount = g_minImageCount;
	imguiInfo.ImageCount = swapChain.Capabilities().maxImageCount; // set larger count to prevent validation error from CreateOrResizeBuffer(..) - line.523 in imgui_impl_vulkan.cpp
	imguiInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	imguiInfo.CheckVkResultFn = ThrowIfFailed;
	ImGui_ImplVulkan_Init(&imguiInfo);

	ImGui_ImplVulkan_CreateFontsTexture();
}

void DrawUI(vk::CommandBuffer& cmdBuffer)
{
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuffer.vkCommandBuffer());
	// end main render-pass
	cmdBuffer.EndRenderPass();
}

void Destroy(VkDevice vkDevice)
{
	ImGui_ImplVulkan_Shutdown();
	vkDestroyDescriptorPool(vkDevice, g_vkSrvDescPool, nullptr);
}

} // namespace ImGui

namespace vk
{

Renderer::Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext)
{
	assert(pWindow);

	m_pRenderContext = new RenderContext();
	m_pSwapChain = new SwapChain(*m_pRenderContext, *pWindow);

	ForwardPass::Initialize(*m_pRenderContext);
	ImGui::InitUI(*m_pRenderContext, *m_pSwapChain, pImGuiContext);

	printf("VkRenderer constructed!\n");
}

Renderer::~Renderer()
{
	m_pRenderContext->GraphicsQueue().Flush();
	m_pRenderContext->ComputeQueue().Flush();
	if (m_pRenderContext->TransferQueue()) 
		m_pRenderContext->TransferQueue()->Flush();
	vkDeviceWaitIdle(m_pRenderContext->vkDevice());

	ImGui::Destroy(m_pRenderContext->vkDevice());
	ForwardPass::Destroy();

	RELEASE(m_pSwapChain);
	RELEASE(m_pRenderContext);

	printf("VkRenderer destructed!\n");
}

void Renderer::Render()
{
	auto& cmdBuffer = BeginFrame();
	RenderFrame(cmdBuffer);
	EndFrame(cmdBuffer);
}

void Renderer::NewFrame()
{
	ImGui_ImplVulkan_NewFrame();
}

void Renderer::SetRendererType(eRendererType type)
{
	m_type = type;
}

void Renderer::OnWindowResized(i32 width, i32 height)
{
	if (width == 0 || height == 0)
		return;

	VK_CHECK(vkDeviceWaitIdle(m_pRenderContext->vkDevice()));
	m_pSwapChain->ResizeViewport();

	ForwardPass::Resize(width, height);
}

CommandBuffer& Renderer::BeginFrame()
{
	auto& cmdBuffer = m_pRenderContext->GraphicsQueue().Allocate();
	m_pSwapChain->AcquireImageIndex(cmdBuffer.vkPresentCompleteSemaphore());

	return cmdBuffer;
}

void Renderer::RenderFrame(CommandBuffer& cmdBuffer)
{
	{
		ForwardPass::Apply(cmdBuffer);
	}
	ImGui::DrawUI(cmdBuffer);
}

void Renderer::EndFrame(CommandBuffer& cmdBuffer)
{
	auto& rm = m_pRenderContext->GetResourceManager();
	auto backBuffer = m_pSwapChain->GetImageToPresent();
	auto mainTarget = ForwardPass::GetRenderedTexture(eAttachmentPoint::Color0);

	cmdBuffer.CopyTexture(rm.Get(backBuffer), rm.Get(mainTarget));
	cmdBuffer.TransitionImageLayout(
		rm.Get(backBuffer),
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 
		VK_PIPELINE_STAGE_2_COPY_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	cmdBuffer.Close();

	m_pRenderContext->GraphicsQueue().ExecuteCommandBuffer(cmdBuffer);
	m_pSwapChain->Present(cmdBuffer.vkRenderCompleteSemaphore());
}

} // namespace vk
