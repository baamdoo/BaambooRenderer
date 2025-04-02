#pragma once
#include "BaambooCore/ResourceHandle.h"

namespace baamboo
{
	class Window;
}

namespace vk
{

class Texture;

class SwapChain
{
public:
	explicit SwapChain(RenderContext& context, baamboo::Window& window);
	~SwapChain();

	u32 AcquireImageIndex(VkSemaphore vkPresentCompleteSemaphore);
	void Present(VkSemaphore vkRenderCompleteSemaphore);

	void ResizeViewport();

	[[nodiscard]]
	inline VkSwapchainKHR vkSwapChain() const { return m_vkSwapChain; }
	[[nodiscard]]
	inline VkSurfaceCapabilitiesKHR Capabilities() const { return m_capabilities; }

	[[nodiscard]]
	inline baamboo::ResourceHandle< Texture > GetImageToPresent() const { return m_textures[m_imageIndex]; }

	[[nodiscard]]
	inline u32 ImageCount() const { return m_imageCount; }

private:
	void Init();
	void Release(VkSwapchainKHR vkSwapChain);

private:
	RenderContext&   m_renderContext;
	baamboo::Window& m_window;

	VkFormat                 m_imageFormat = VK_FORMAT_UNDEFINED;
	VkSurfaceKHR             m_vkSurface = VK_NULL_HANDLE;
	VkSwapchainKHR           m_vkSwapChain = VK_NULL_HANDLE;
	VkSurfaceCapabilitiesKHR m_capabilities;

	u32 m_imageCount;
	u32 m_imageIndex;
	bool m_vSync = true;
	bool m_bResized = false;

	std::vector< baamboo::ResourceHandle< Texture > > m_textures;
};

} // namespace vk