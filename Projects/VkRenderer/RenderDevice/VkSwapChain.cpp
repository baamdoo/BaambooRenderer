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
	: m_renderContext(context)
	, m_window(window)
{
	VkWin32SurfaceCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hwnd = m_window.WinHandle();
	createInfo.hinstance = GetModuleHandle(nullptr);
	VK_CHECK(vkCreateWin32SurfaceKHR(m_renderContext.vkInstance(), &createInfo, nullptr, &m_vkSurface));
	
	Init();
}

SwapChain::~SwapChain()
{
	Release(m_vkSwapChain);
	vkDestroySurfaceKHR(m_renderContext.vkInstance(), m_vkSurface, nullptr);
}

u32 SwapChain::AcquireImageIndex(VkSemaphore vkPresentCompleteSemaphore)
{
	VK_CHECK(vkAcquireNextImageKHR(m_renderContext.vkDevice(), m_vkSwapChain, UINT64_MAX, vkPresentCompleteSemaphore, (VkFence)nullptr, &m_imageIndex));
	return m_imageIndex;
}

void SwapChain::Present(VkSemaphore vkRenderCompleteSemaphore)
{
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &vkRenderCompleteSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_vkSwapChain;
	presentInfo.pImageIndices = &m_imageIndex;
	VkResult presentResult = vkQueuePresentKHR(m_renderContext.GraphicsQueue().vkQueue(), &presentInfo);

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
	if (m_window.Width() == 0 || m_window.Height() == 0)
		return;

	Init();
}

void SwapChain::Init()
{
	// **
	// Swap-chain
	// **
	VkSwapchainKHR oldSwapchain = m_vkSwapChain;

	SwapChainBuilder swapChainBuilder(m_vkSurface, m_renderContext.vkPhysicalDevice());
	m_vkSwapChain = swapChainBuilder.SetDesiredImageResolution(m_window.Width(), m_window.Height())
		                            .SetDesiredImageCount(m_window.Desc().numDesiredImages)
		                            .SetDesiredImageFormat(VK_FORMAT_R8G8B8A8_UNORM)
		                            .SetVSync(m_window.Desc().bVSync)
		                            .AddImageUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		                            .Build(m_renderContext.vkDevice(), m_renderContext.GraphicsQueue().Index(), oldSwapchain);
	m_imageCount = swapChainBuilder.imageCount;
	m_imageFormat = swapChainBuilder.selectedSurface.format;
	m_capabilities = swapChainBuilder.capabilities;
	if (oldSwapchain)
		Release(oldSwapchain);


	// **
	// Textures
	// **
	auto& rm = m_renderContext.GetResourceManager();

	std::vector< VkImage > images(m_imageCount);
	std::vector< VkImageView > imageViews(m_imageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(m_renderContext.vkDevice(), m_vkSwapChain, &m_imageCount, images.data()));

	m_textures.resize(m_imageCount);
	for (u32 i = 0; i < m_imageCount; ++i)
	{
		VkImageViewCreateInfo imageViewInfo = {};
		imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewInfo.image = images[i];
		imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewInfo.format = m_imageFormat;
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
		VK_CHECK(vkCreateImageView(m_renderContext.vkDevice(), &imageViewInfo, nullptr, &imageViews[i]));

		auto pTex = rm.CreateEmpty< Texture >(L"SwapChainBuffer_" + std::to_wstring(i));
		pTex->SetResource(images[i], imageViews[i], VK_NULL_HANDLE);

		m_textures[i] = rm.Add(pTex);
	}

	// update values in render-context to easily be referenced by other vk-components
	m_renderContext.SetNumContexts(m_imageCount);
	m_renderContext.SetViewportWidth(m_window.Width());
	m_renderContext.SetViewportHeight(m_window.Height());
}

void SwapChain::Release(VkSwapchainKHR vkSwapChain)
{
	m_textures.clear();
	vkDestroySwapchainKHR(m_renderContext.vkDevice(), vkSwapChain, nullptr);
}

} // namespace vk