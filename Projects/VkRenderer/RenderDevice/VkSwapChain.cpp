#include "RendererPch.h"
#include "VkSwapChain.h"
#include "VkBuildHelpers.h"
#include "VkCommandQueue.h"
#include "RenderResource/VkTexture.h"

#if defined(_WIN32)
	#define VK_USE_PLATFORM_WIN32_KHR
	#include <Windows.h>
	#include <vulkan/vulkan_win32.h>
#endif
#define GLFW_INCLUDE_VULKAN
#include <BaambooCore/Window.h>

namespace vk
{

SwapChain::SwapChain(VkRenderDevice& rd, baamboo::Window& window)
	: m_RenderDevice(rd)
	, m_Window(window)
{
	VkWin32SurfaceCreateInfoKHR createInfo{};
	createInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hwnd      = m_Window.WinHandle();
	createInfo.hinstance = GetModuleHandle(nullptr);
	VK_CHECK(vkCreateWin32SurfaceKHR(m_RenderDevice.vkInstance(), &createInfo, nullptr, &m_vkSurface));
	VkBool32 bPresentationSupported = VK_FALSE;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(
		m_RenderDevice.vkPhysicalDevice(),
		m_RenderDevice.GraphicsQueue().Index(),
		m_vkSurface,
		&bPresentationSupported));
	BB_ASSERT(bPresentationSupported == VK_TRUE, "The selected Vulkan graphics queue cannot present to this surface.");

	BB_ASSERT(Init(), "Failed to create the Vulkan swapchain.");
}

SwapChain::~SwapChain()
{
	if (!Release(m_vkSwapChain))
		printf("Vulkan present-fence wait failed during swapchain destruction.\n");
	vkDestroySurfaceKHR(m_RenderDevice.vkInstance(), m_vkSurface, nullptr);
}

u32 SwapChain::AcquireNextImage(VkSemaphore vkPresentCompleteSemaphore)
{
	if (m_bResized)
	{
		if (!IsRenderable())
			return kInvalidIndex;

		ResizeViewport();
		if (m_bResized)
			return kInvalidIndex;
	}

	for (;;)
	{
		if (!IsRenderable())
		{
			m_bResized = true;
			return kInvalidIndex;
		}

		const VkResult result =
			vkAcquireNextImageKHR(m_RenderDevice.vkDevice(), m_vkSwapChain, UINT64_MAX,
				vkPresentCompleteSemaphore, VK_NULL_HANDLE, &m_ImageIndex);

		if (result == VK_SUCCESS)
			break;

		if (result == VK_SUBOPTIMAL_KHR)
		{
			m_bResized = true;
			break;
		}

		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			m_bResized = true;
			if (!IsRenderable())
				return kInvalidIndex;

			ResizeViewport();
			if (m_bResized)
				return kInvalidIndex;
			continue;
		}

		VK_CHECK(result);
		return kInvalidIndex;
	}

	BB_ASSERT(m_ImageIndex < m_BackBuffers.size(), "Vulkan acquired an invalid swapchain image index.");

	// Reset tracked state for the acquired image. Layout is set to UNDEFINED
	// since we discard the previous frame's contents on the next transition.
	m_BackBuffers[m_ImageIndex]->SetState(BarrierState(0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_UNDEFINED));

	return m_ImageIndex;
}

void SwapChain::Present(VkSemaphore vkRenderCompleteSemaphore)
{
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

	if (vkRenderCompleteSemaphore != VK_NULL_HANDLE)
	{
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores    = &vkRenderCompleteSemaphore;
	}

	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains    = &m_vkSwapChain;
	presentInfo.pImageIndices  = &m_ImageIndex;

	VkFence vkPresentFence = VK_NULL_HANDLE;
	VkSwapchainPresentFenceInfoEXT presentFenceInfo = {};
	if (m_RenderDevice.Capabilities().bSwapchainMaintenance)
	{
		vkPresentFence = AcquirePresentFence();

		presentFenceInfo.sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
		presentFenceInfo.swapchainCount = 1;
		presentFenceInfo.pFences        = &vkPresentFence;

		presentInfo.pNext = &presentFenceInfo;
	}

	const VkResult presentResult = vkQueuePresentKHR(m_RenderDevice.GraphicsQueue().vkQueue(), &presentInfo);
	if (vkPresentFence != VK_NULL_HANDLE)
	{
		const bool bPresentEnqueued =
			presentResult == VK_SUCCESS ||
			presentResult == VK_SUBOPTIMAL_KHR ||
			presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
			presentResult == VK_ERROR_SURFACE_LOST_KHR ||
			presentResult == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT;
		(bPresentEnqueued ? m_PendingPresentFences : m_AvailablePresentFences).push_back(vkPresentFence);
	}

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
	{
		m_bResized = true;
	}
	else if (presentResult != VK_SUCCESS)
	{
		VK_CHECK(presentResult);
	}
}

VkSemaphore SwapChain::PresentWaitSemaphore(u32 imageIndex) const
{
	BB_ASSERT(imageIndex < m_PresentWaitSemaphores.size(), "Invalid Vulkan swapchain image index.");
	return m_PresentWaitSemaphores[imageIndex];
}

bool SwapChain::IsRenderable() const
{
	return m_vkSwapChain != VK_NULL_HANDLE &&
		!m_Window.Minimized() &&
		m_Window.Width() > 0 &&
		m_Window.Height() > 0;
}

void SwapChain::ResizeViewport()
{
	m_bResized = true;

	if (m_Window.Minimized() || m_Window.Width() == 0 || m_Window.Height() == 0)
	{
		m_bResized = true;
		return;
	}

	const VkResult idleResult = vkDeviceWaitIdle(m_RenderDevice.vkDevice());
	if (idleResult != VK_SUCCESS)
	{
		VK_CHECK(idleResult);
		return;
	}

	if (Init())
		m_bResized = false;
}

Arc< VulkanTexture > SwapChain::GetImageToPresent() const
{
	return m_BackBuffers[m_ImageIndex];
}

bool SwapChain::Init()
{
	// **
	// Swap-chain
	// **
	VkSwapchainKHR oldSwapchain = m_vkSwapChain;

	SwapChainBuilder swapChainBuilder(m_vkSurface, m_RenderDevice.vkPhysicalDevice());
	const VkSwapchainKHR newSwapchain =
		swapChainBuilder.SetDesiredImageResolution(m_Window.Width(), m_Window.Height())
		                .SetDesiredImageCount(m_Window.Desc().numDesiredImages)
		                .SetDesiredImageFormat(VK_FORMAT_R8G8B8A8_UNORM)
		                .SetVSync(m_Window.Desc().bVSync)
		                .AddImageUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		                .Build(m_RenderDevice.vkDevice(), m_RenderDevice.GraphicsQueue().Index(), oldSwapchain);
	if (newSwapchain == VK_NULL_HANDLE)
		return false;

	const VkFormat newImageFormat = swapChainBuilder.selectedSurface.format;
	const VkExtent2D swapChainExtent = swapChainBuilder.Extent();
	u32 newImageCount = 0;
	const VkResult imageCountResult =
		vkGetSwapchainImagesKHR(m_RenderDevice.vkDevice(), newSwapchain, &newImageCount, nullptr);
	if (imageCountResult != VK_SUCCESS)
	{
		VK_CHECK(imageCountResult);
		vkDestroySwapchainKHR(m_RenderDevice.vkDevice(), newSwapchain, nullptr);
		return false;
	}

	if (oldSwapchain)
	{
		BB_ASSERT(newImageCount == m_ImageCount,
			"Changing the Vulkan swapchain image count requires ImGui backend reinitialization.");
		BB_ASSERT(newImageFormat == m_ImageFormat,
			"Changing the Vulkan swapchain format requires render-pipeline reinitialization.");
		if (!Release(oldSwapchain))
		{
			vkDestroySwapchainKHR(m_RenderDevice.vkDevice(), newSwapchain, nullptr);
			return false;
		}
	}

	m_vkSwapChain = newSwapchain;
	m_ImageFormat = newImageFormat;
	m_Capabilities = swapChainBuilder.capabilities;
	m_ImageCount = newImageCount;

	// **
	// Textures
	// **
	std::vector< VkImage > images(m_ImageCount);
	std::vector< VkImageView > imageViews(m_ImageCount);
	u32 queriedImageCount = m_ImageCount;
	const VkResult imagesResult =
		vkGetSwapchainImagesKHR(m_RenderDevice.vkDevice(), m_vkSwapChain, &queriedImageCount, images.data());
	BB_ASSERT(imagesResult == VK_SUCCESS && queriedImageCount == m_ImageCount,
		"Failed to retrieve the expected Vulkan swapchain images.");

	m_BackBuffers.resize(m_ImageCount);
	m_PresentWaitSemaphores.resize(m_ImageCount);
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	for (u32 i = 0; i < m_ImageCount; ++i)
	{
		VK_CHECK(vkCreateSemaphore(m_RenderDevice.vkDevice(), &semaphoreInfo, nullptr, &m_PresentWaitSemaphores[i]));

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
		VK_CHECK(vkCreateImageView(m_RenderDevice.vkDevice(), &imageViewInfo, nullptr, &imageViews[i]));

		auto pTex = VulkanTexture::CreateEmpty(m_RenderDevice, std::string("SwapChainBuffer_" + std::to_string(i)).c_str());
		pTex->SetResource(
			images[i],
			imageViews[i],
			{
				.imageType = VK_IMAGE_TYPE_2D,
				.format    = m_ImageFormat,
				.extent    = { swapChainExtent.width, swapChainExtent.height, 1 },
			}, 
			VK_NULL_HANDLE,
			{},
			VK_IMAGE_ASPECT_COLOR_BIT);
		
		m_BackBuffers[i] = pTex;
	}

	// update values in render-context to easily be referenced by other vk-components
	m_RenderDevice.SetNumContexts(m_ImageCount);
	m_RenderDevice.SetWindowWidth(swapChainExtent.width);
	m_RenderDevice.SetWindowHeight(swapChainExtent.height);
	return true;
}

VkFence SwapChain::AcquirePresentFence()
{
	BB_ASSERT(m_RenderDevice.Capabilities().bSwapchainMaintenance,
		"Present fences require swapchainMaintenance1.");
	CollectPresentFences();

	if (!m_AvailablePresentFences.empty())
	{
		const VkFence vkFence = m_AvailablePresentFences.back();
		m_AvailablePresentFences.pop_back();
		return vkFence;
	}

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	VkFence vkFence = VK_NULL_HANDLE;
	const VkResult result = vkCreateFence(m_RenderDevice.vkDevice(), &fenceInfo, nullptr, &vkFence);
	BB_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan present fence (%d).", static_cast<i32>(result));
	return vkFence;
}

void SwapChain::CollectPresentFences()
{
	auto it = m_PendingPresentFences.begin();
	while (it != m_PendingPresentFences.end())
	{
		const VkResult status = vkGetFenceStatus(m_RenderDevice.vkDevice(), *it);
		if (status == VK_NOT_READY)
		{
			++it;
			continue;
		}
		if (status != VK_SUCCESS)
		{
			VK_CHECK(status);
			++it;
			continue;
		}

		const VkFence vkFence = *it;
		const VkResult resetResult = vkResetFences(m_RenderDevice.vkDevice(), 1, &vkFence);
		if (resetResult != VK_SUCCESS)
		{
			VK_CHECK(resetResult);
			++it;
			continue;
		}

		m_AvailablePresentFences.push_back(vkFence);
		it = m_PendingPresentFences.erase(it);
	}
}

bool SwapChain::WaitForPresentFences()
{
	if (m_PendingPresentFences.empty())
		return true;

	const VkResult result = vkWaitForFences(
		m_RenderDevice.vkDevice(),
		static_cast<u32>(m_PendingPresentFences.size()),
		m_PendingPresentFences.data(),
		VK_TRUE,
		UINT64_MAX);
	if (result != VK_SUCCESS)
	{
		VK_CHECK(result);
		return false;
	}
	return true;
}

void SwapChain::DestroyPresentFences()
{
	for (const VkFence vkFence : m_PendingPresentFences)
		vkDestroyFence(m_RenderDevice.vkDevice(), vkFence, nullptr);
	for (const VkFence vkFence : m_AvailablePresentFences)
		vkDestroyFence(m_RenderDevice.vkDevice(), vkFence, nullptr);
	m_PendingPresentFences.clear();
	m_AvailablePresentFences.clear();
}

bool SwapChain::Release(VkSwapchainKHR vkSwapChain)
{
	if (!WaitForPresentFences())
		return false;
	DestroyPresentFences();

	for (VkSemaphore semaphore : m_PresentWaitSemaphores)
		if (semaphore)
			vkDestroySemaphore(m_RenderDevice.vkDevice(), semaphore, nullptr);
	m_PresentWaitSemaphores.clear();

	m_BackBuffers.clear();
	if (vkSwapChain)
		vkDestroySwapchainKHR(m_RenderDevice.vkDevice(), vkSwapChain, nullptr);

	m_ImageCount = 0;
	m_ImageIndex = kInvalidIndex;
	return true;
}

} // namespace vk