#pragma once

namespace vk
{

class CommandQueue;
class CommandBuffer;
class ResourceManager;

class RenderContext
{
public:
	RenderContext();
	~RenderContext();

	u8 NextFrame();

	[[nodiscard]]
	VkDeviceSize GetAlignedSize(VkDeviceSize size) const;

	[[nodiscard]]
	inline VkInstance vkInstance() const { return m_vkInstance; }
	[[nodiscard]]
	inline VkDevice vkDevice() const { return m_vkDevice; }
	[[nodiscard]]
	inline VkPhysicalDevice vkPhysicalDevice() const { return m_vkPhysicalDevice; }
	[[nodiscard]]
	inline const VkPhysicalDeviceProperties& DeviceProps() const { return m_physicalDeviceProperties; }

	[[nodiscard]]
	inline CommandQueue& GraphicsQueue() const { return *m_pGraphicsQueue; }
	[[nodiscard]]
	inline CommandQueue& ComputeQueue() const { return *m_pComputeQueue; }
	[[nodiscard]]
	inline CommandQueue* TransferQueue() const { return m_pTransferQueue; }

	[[nodiscard]]
	inline VmaAllocator vmaAllocator() const { return m_vmaAllocator; }
	[[nodiscard]]
	inline ResourceManager& GetResourceManager() const { return *m_pResourceManager; }

	[[nodiscard]]
	inline VkRenderPass vkMainRenderPass() const { return m_vkMainRenderPass; }
	void SetMainRenderPass(VkRenderPass vkRenderPass) { m_vkMainRenderPass = vkRenderPass; }
	[[nodiscard]]
	inline VkDescriptorPool vkStaticDescriptorPool() const { return m_vkStaticDescriptorPool; }

	[[nodiscard]]
	inline u8 ContextIndex() const { return m_contextIndex; }
	[[nodiscard]]
	inline u32 NumContexts() const { return m_numContexts; }
	void SetNumContexts(u32 num) { m_numContexts = num; }

	[[nodiscard]]
	u32 ViewportWidth() const { return m_viewportWidth; }
	void SetViewportWidth(u32 width) { m_viewportWidth = width; }
	[[nodiscard]]
	u32 ViewportHeight() const { return m_viewportHeight; }
	void SetViewportHeight(u32 height) { m_viewportHeight = height; }

	void SetVkObjectName(std::string_view name, u64 handle, VkObjectType type);

private:
	VkInstance					m_vkInstance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT	m_vkDebugMessenger = VK_NULL_HANDLE;
								
	VkPhysicalDevice			m_vkPhysicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties	m_physicalDeviceProperties;
	VkDevice					m_vkDevice = VK_NULL_HANDLE;

	CommandQueue* m_pGraphicsQueue = nullptr;
	CommandQueue* m_pComputeQueue = nullptr;
	CommandQueue* m_pTransferQueue = nullptr;

	VmaAllocator     m_vmaAllocator = VK_NULL_HANDLE;
	ResourceManager* m_pResourceManager = nullptr;

	VkRenderPass     m_vkMainRenderPass = VK_NULL_HANDLE;
	VkDescriptorPool m_vkStaticDescriptorPool = VK_NULL_HANDLE;

	u32 m_numContexts = 0;
	u8  m_contextIndex = 0;

	u32 m_viewportWidth = 0;
	u32 m_viewportHeight = 0;
};

} // namespace vk