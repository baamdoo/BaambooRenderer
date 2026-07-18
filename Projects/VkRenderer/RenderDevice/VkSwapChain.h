#pragma once

namespace baamboo
{
	class Window;
}

namespace vk
{

class VulkanTexture;

class SwapChain
{
public:
	explicit SwapChain(VkRenderDevice& rd, baamboo::Window& window);
	~SwapChain();

	u32 AcquireNextImage(VkSemaphore vkPresentCompleteSemaphore);
	void Present(VkSemaphore vkRenderCompleteSemaphore);
	VkSemaphore PresentWaitSemaphore(u32 imageIndex) const;

	void ResizeViewport();

	[[nodiscard]]
	inline VkSwapchainKHR vkSwapChain() const { return m_vkSwapChain; }
	[[nodiscard]]
	inline VkSurfaceCapabilitiesKHR Capabilities() const { return m_Capabilities; }

	Arc< VulkanTexture > GetImageToPresent() const;

	[[nodiscard]]
	inline VkFormat ImageFormat() const { return m_ImageFormat; }
	[[nodiscard]]
	inline u32 ImageCount() const { return m_ImageCount; }
	[[nodiscard]]
	bool IsRenderable() const;

private:
	bool Init();
	bool Release(VkSwapchainKHR vkSwapChain);
	VkFence AcquirePresentFence();
	void CollectPresentFences();
	bool WaitForPresentFences();
	void DestroyPresentFences();

private:
	VkRenderDevice&  m_RenderDevice;
	baamboo::Window& m_Window;

	VkFormat                 m_ImageFormat = VK_FORMAT_UNDEFINED;
	VkSurfaceKHR             m_vkSurface   = VK_NULL_HANDLE;
	VkSwapchainKHR           m_vkSwapChain = VK_NULL_HANDLE;
	VkSurfaceCapabilitiesKHR m_Capabilities;

	u32 m_ImageCount = 0;
	u32 m_ImageIndex = kInvalidIndex;

	std::vector< Arc< VulkanTexture > > m_BackBuffers;
	std::vector< VkSemaphore > m_PresentWaitSemaphores;

	std::vector< VkFence > m_AvailablePresentFences;
	std::vector< VkFence > m_PendingPresentFences;

	bool m_vSync    = true;
	bool m_bResized = false;

};

} // namespace vk