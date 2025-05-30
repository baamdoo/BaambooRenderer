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
	inline VkSurfaceCapabilitiesKHR Capabilities() const { return m_Capabilities; }

	[[nodiscard]]
	inline baamboo::ResourceHandle< Texture > GetImageToPresent() const { return m_BackBuffers[m_ImageIndex]; }

	[[nodiscard]]
	inline u32 ImageCount() const { return m_ImageCount; }
	[[nodiscard]]
	inline bool IsRenderable() const { return m_vkSwapChain != VK_NULL_HANDLE; }

private:
	void Init();
	void Release(VkSwapchainKHR vkSwapChain);

private:
	RenderContext&   m_RenderContext;
	baamboo::Window& m_Window;

	VkFormat                 m_ImageFormat = VK_FORMAT_UNDEFINED;
	VkSurfaceKHR             m_vkSurface = VK_NULL_HANDLE;
	VkSwapchainKHR           m_vkSwapChain = VK_NULL_HANDLE;
	VkSurfaceCapabilitiesKHR m_Capabilities;

	u32 m_ImageCount;
	u32 m_ImageIndex;
	bool m_vSync = true;
	bool m_bResized = false;

	std::vector< baamboo::ResourceHandle< Texture > > m_BackBuffers;
};

} // namespace vk