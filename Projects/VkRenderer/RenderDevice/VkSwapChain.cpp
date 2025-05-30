#include "RendererPch.h"
#include "VkSwapChain.h"
#include "VkBuildHelpers.h"
#include "VkCommandQueue.h"
#include "VkResourceManager.h"

#if defined(_WIN32)
	#define VK_USE_PLATFORM_WIN32_KHR
	#include <Windows.h>
	#include <vulkan/vulkan_win32.h>
#endif
#define GLFW_INCLUDE_VULKAN
#include <BaambooCore/Window.h>

namespace vk
{

SwapChain::SwapChain(RenderContext& context, baamboo::Window& window)
	: m_RenderContext(context)
	, m_Window(window)
{
	VkWin32SurfaceCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hwnd = m_Window.WinHandle();
	createInfo.hinstance = GetModuleHandle(nullptr);
	VK_CHECK(vkCreateWin32SurfaceKHR(m_RenderContext.vkInstance(), &createInfo, nullptr, &m_vkSurface));
	
	Init();
}

SwapChain::~SwapChain()
{
	Release(m_vkSwapChain);
	vkDestroySurfaceKHR(m_RenderContext.vkInstance(), m_vkSurface, nullptr);
}

u32 SwapChain::AcquireImageIndex(VkSemaphore vkPresentCompleteSemaphore)
{
	VK_CHECK(vkAcquireNextImageKHR(m_RenderContext.vkDevice(), m_vkSwapChain, UINT64_MAX, vkPresentCompleteSemaphore, (VkFence)nullptr, &m_ImageIndex));
	return m_ImageIndex;
}

void SwapChain::Present(VkSemaphore vkRenderCompleteSemaphore)
{
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &vkRenderCompleteSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_vkSwapChain;
	presentInfo.pImageIndices = &m_ImageIndex;
	VkResult presentResult = vkQueuePresentKHR(m_RenderContext.GraphicsQueue().vkQueue(), &presentInfo);

	if (presentResult != VK_SUCCESS) 
	{
		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
			ResizeViewport();
		else
			VK_CHECK(presentResult);
	}
}

void SwapChain::ResizeViewport()
{
	if (m_Window.Width() == 0 || m_Window.Height() == 0)
		return;

	Init();
}

void SwapChain::Init()
{
	// **
	// Swap-chain
	// **
	VkSwapchainKHR oldSwapchain = m_vkSwapChain;

	SwapChainBuilder swapChainBuilder(m_vkSurface, m_RenderContext.vkPhysicalDevice());
	m_vkSwapChain = swapChainBuilder.SetDesiredImageResolution(m_Window.Width(), m_Window.Height())
		                            .SetDesiredImageCount(m_Window.Desc().numDesiredImages)
		                            .SetDesiredImageFormat(VK_FORMAT_R8G8B8A8_UNORM)
		                            .SetVSync(m_Window.Desc().bVSync)
		                            .AddImageUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		                            .Build(m_RenderContext.vkDevice(), m_RenderContext.GraphicsQueue().Index(), oldSwapchain);

	m_ImageCount = swapChainBuilder.imageCount;
	m_ImageFormat = swapChainBuilder.selectedSurface.format;
	m_Capabilities = swapChainBuilder.capabilities;
	if (oldSwapchain)
		Release(oldSwapchain);

	if (m_vkSwapChain == VK_NULL_HANDLE)
	{
		return;
	}

	// **
	// Textures
	// **
	auto& rm = m_RenderContext.GetResourceManager();

	std::vector< VkImage > images(m_ImageCount);
	std::vector< VkImageView > imageViews(m_ImageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(m_RenderContext.vkDevice(), m_vkSwapChain, &m_ImageCount, images.data()));

	m_BackBuffers.resize(m_ImageCount);
	for (u32 i = 0; i < m_ImageCount; ++i)
	{
		VkImageViewCreateInfo imageViewInfo = {};
		imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewInfo.image = images[i];
		imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewInfo.format = m_ImageFormat;
		imageViewInfo.components = {
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B,
			VK_COMPONENT_SWIZZLE_A
		};
		imageViewInfo.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1
		};
		VK_CHECK(vkCreateImageView(m_RenderContext.vkDevice(), &imageViewInfo, nullptr, &imageViews[i]));

		auto pTex = rm.CreateEmpty< Texture >(L"SwapChainBuffer_" + std::to_wstring(i));
		pTex->SetResource(images[i], imageViews[i], VK_NULL_HANDLE, VK_IMAGE_ASPECT_COLOR_BIT);

		m_BackBuffers[i] = rm.Add(pTex);
	}

	// update values in render-context to easily be referenced by other vk-components
	m_RenderContext.SetNumContexts(m_ImageCount);
	m_RenderContext.SetWindowWidth(m_Window.Width());
	m_RenderContext.SetWindowHeight(m_Window.Height());
}

void SwapChain::Release(VkSwapchainKHR vkSwapChain)
{
	m_BackBuffers.clear();
	vkDestroySwapchainKHR(m_RenderContext.vkDevice(), vkSwapChain, nullptr);
}

} // namespace vk