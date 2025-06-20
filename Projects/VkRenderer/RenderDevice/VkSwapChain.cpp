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

static PFN_vkAcquireNextImage2KHR      fnAcquireNextImage2      = nullptr;
static PFN_vkQueuePresentKHR           fnQueuePresent2          = nullptr;
static PFN_vkGetSwapchainStatusKHR     fnGetSwapchainStatus     = nullptr;
static PFN_vkReleaseSwapchainImagesEXT fnReleaseSwapchainImages = nullptr;

SwapChain::SwapChain(RenderDevice& device, baamboo::Window& window)
	: m_RenderDevice(device)
	, m_Window(window)
{
	VkWin32SurfaceCreateInfoKHR createInfo{};
	createInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hwnd      = m_Window.WinHandle();
	createInfo.hinstance = GetModuleHandle(nullptr);
	VK_CHECK(vkCreateWin32SurfaceKHR(m_RenderDevice.vkInstance(), &createInfo, nullptr, &m_vkSurface));

	u32 deviceExtCount;
	vkEnumerateDeviceExtensionProperties(device.vkPhysicalDevice(), nullptr, &deviceExtCount, nullptr);
	std::vector<VkExtensionProperties> deviceExts(deviceExtCount);
	vkEnumerateDeviceExtensionProperties(device.vkPhysicalDevice(), nullptr, &deviceExtCount, deviceExts.data());

	for (const auto& ext : deviceExts) 
	{
		if (strcmp(ext.extensionName, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) == 0)
			m_bHasMaintenance = true;
	}

	if (m_bHasMaintenance) 
	{
		fnAcquireNextImage2      = reinterpret_cast<PFN_vkAcquireNextImage2KHR>(vkGetDeviceProcAddr(device.vkDevice(), "vkAcquireNextImage2KHR"));
		fnGetSwapchainStatus     = reinterpret_cast<PFN_vkGetSwapchainStatusKHR>(vkGetDeviceProcAddr(device.vkDevice(), "vkGetSwapchainStatusKHR"));
		fnReleaseSwapchainImages = reinterpret_cast<PFN_vkReleaseSwapchainImagesEXT>(vkGetDeviceProcAddr(device.vkDevice(), "vkReleaseSwapchainImagesEXT"));

		VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance1Features = {};
		maintenance1Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;

		VkPhysicalDeviceFeatures2 features2 = {};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &maintenance1Features;
		vkGetPhysicalDeviceFeatures2(device.vkPhysicalDevice(), &features2);

		m_bHasMaintenance = maintenance1Features.swapchainMaintenance1;

		VkPhysicalDevicePresentIdFeaturesKHR presentIdFeatures = {};
		presentIdFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
		features2.pNext         = &presentIdFeatures;
		vkGetPhysicalDeviceFeatures2(device.vkPhysicalDevice(), &features2);

		m_bHasPresentFence = presentIdFeatures.presentId;
	}

	Init();
}

SwapChain::~SwapChain()
{
	Release(m_vkSwapChain);
	vkDestroySurfaceKHR(m_RenderDevice.vkInstance(), m_vkSurface, nullptr);
}

u32 SwapChain::AcquireNextImage(VkSemaphore vkPresentCompleteSemaphore)
{
	if (m_bHasMaintenance && fnAcquireNextImage2) 
	{
		VkResult result = 
			vkAcquireNextImageKHR(m_RenderDevice.vkDevice(), m_vkSwapChain, UINT_MAX, vkPresentCompleteSemaphore, VK_NULL_HANDLE, &m_ImageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) 
		{
			ResizeViewport();
			return AcquireNextImage(vkPresentCompleteSemaphore);
		}
		VK_CHECK(result);
	}
	else 
	{
		assert(false);
	}

	return m_ImageIndex;
}

void SwapChain::Present(VkSemaphore vkRenderCompleteSemaphore, VkFence vkSignalFence)
{
	if (m_bHasMaintenance && m_bHasPresentFence) 
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

		VkSwapchainPresentFenceInfoEXT fenceInfo = {};
		fenceInfo.sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
		fenceInfo.swapchainCount = 1;
		fenceInfo.pFences        = &vkSignalFence;

		presentInfo.pNext = &fenceInfo;

		VkResult presentResult = vkQueuePresentKHR(m_RenderDevice.GraphicsQueue().vkQueue(), &presentInfo);
		if (presentResult != VK_SUCCESS)
		{
			if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
				ResizeViewport();
			else
				VK_CHECK(presentResult);
		}
	}
	else
	{
		assert(false);
	}

	/*VkPresentInfoKHR presentInfo   = {};
	presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores    = &vkRenderCompleteSemaphore;
	presentInfo.swapchainCount     = 1;
	presentInfo.pSwapchains        = &m_vkSwapChain;
	presentInfo.pImageIndices      = &m_ImageIndex;

	VkResult presentResult = vkQueuePresentKHR(m_RenderDevice.GraphicsQueue().vkQueue(), &presentInfo);
	if (presentResult != VK_SUCCESS) 
	{
		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
			ResizeViewport();
		else
			VK_CHECK(presentResult);
	}*/
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

	SwapChainBuilder swapChainBuilder(m_vkSurface, m_RenderDevice.vkPhysicalDevice());
	m_vkSwapChain = swapChainBuilder.SetDesiredImageResolution(m_Window.Width(), m_Window.Height())
		                            .SetDesiredImageCount(m_Window.Desc().numDesiredImages)
		                            .SetDesiredImageFormat(VK_FORMAT_R8G8B8A8_UNORM)
		                            .SetVSync(m_Window.Desc().bVSync)
		                            .AddImageUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		                            .Build(m_RenderDevice.vkDevice(), m_RenderDevice.GraphicsQueue().Index(), oldSwapchain);

	m_ImageCount   = swapChainBuilder.imageCount;
	m_ImageFormat  = swapChainBuilder.selectedSurface.format;
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
	std::vector< VkImage > images(m_ImageCount);
	std::vector< VkImageView > imageViews(m_ImageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(m_RenderDevice.vkDevice(), m_vkSwapChain, &m_ImageCount, images.data()));

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
		VK_CHECK(vkCreateImageView(m_RenderDevice.vkDevice(), &imageViewInfo, nullptr, &imageViews[i]));

		auto pTex = Texture::CreateEmpty(m_RenderDevice, "SwapChainBuffer_" + std::to_string(i));
		pTex->SetResource(
			images[i],
			imageViews[i],
			{
				.imageType = VK_IMAGE_TYPE_2D,
				.format    = m_ImageFormat,
				.extent    = { (u32)m_Window.Desc().width, (u32)m_Window.Desc().height, 1 }, 
			}, 
			VK_NULL_HANDLE, 
			VK_IMAGE_ASPECT_COLOR_BIT);
		
		m_BackBuffers[i] = pTex;
	}

	// update values in render-context to easily be referenced by other vk-components
	m_RenderDevice.SetNumContexts(m_ImageCount);
	m_RenderDevice.SetWindowWidth(m_Window.Width());
	m_RenderDevice.SetWindowHeight(m_Window.Height());
}

void SwapChain::Release(VkSwapchainKHR vkSwapChain)
{
	m_BackBuffers.clear();
	vkDestroySwapchainKHR(m_RenderDevice.vkDevice(), vkSwapChain, nullptr);
}

} // namespace vk