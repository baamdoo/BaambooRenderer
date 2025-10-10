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
	void Present(VkSemaphore vkRenderCompleteSemaphore, VkFence vkSignalFence);

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
	inline bool IsRenderable() const { return m_vkSwapChain != VK_NULL_HANDLE; }

private:
	void Init();
	void Release(VkSwapchainKHR vkSwapChain);

private:
	VkRenderDevice&  m_RenderDevice;
	baamboo::Window& m_Window;

	VkFormat                 m_ImageFormat = VK_FORMAT_UNDEFINED;
	VkSurfaceKHR             m_vkSurface   = VK_NULL_HANDLE;
	VkSwapchainKHR           m_vkSwapChain = VK_NULL_HANDLE;
	VkSurfaceCapabilitiesKHR m_Capabilities;

	u32 m_ImageCount;
	u32 m_ImageIndex;

	std::vector< Arc< VulkanTexture > > m_BackBuffers;

	bool m_vSync    = true;
	bool m_bResized = false;

	bool m_bHasMaintenance   = false;
	bool m_bHasPresentFence  = false;
	bool m_bHasReleaseImages = false;
};

} // namespace vk