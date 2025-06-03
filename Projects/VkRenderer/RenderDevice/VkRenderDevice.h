#pragma once

namespace vk
{

class CommandQueue;
class CommandContext;
class SceneResource;
class ResourceManager;
class DescriptorSet;
class DescriptorPool;

enum class eCommandType;

class RenderDevice
{
public:
	RenderDevice();
	~RenderDevice();

	u8 NextFrame();
	void Flush();

	[[nodiscard]]
	VkDeviceSize GetAlignedSize(VkDeviceSize size) const;

	[[nodiscard]]
	inline VkInstance vkInstance() const { return m_vkInstance; }
	[[nodiscard]]
	inline VkDevice vkDevice() const { return m_vkDevice; }
	[[nodiscard]]
	inline VkPhysicalDevice vkPhysicalDevice() const { return m_vkPhysicalDevice; }
	[[nodiscard]]
	inline const VkPhysicalDeviceProperties& DeviceProps() const { return m_PhysicalDeviceProperties; }

	[[nodiscard]]
	inline CommandQueue& GraphicsQueue() const { return *m_pGraphicsQueue; }
	[[nodiscard]]
	inline CommandQueue& ComputeQueue() const { return *m_pComputeQueue; }
	[[nodiscard]]
	inline CommandQueue& TransferQueue() const { return *m_pTransferQueue; }

	[[nodiscard]]
	CommandContext& BeginCommand(eCommandType cmdType, VkCommandBufferUsageFlags usage = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, bool bTransient = false);

	[[nodiscard]]
	inline VmaAllocator vmaAllocator() const { return m_vmaAllocator; }
	[[nodiscard]]
	inline ResourceManager& GetResourceManager() const { return *m_pResourceManager; }

	[[nodiscard]]
	inline VkRenderPass vkMainRenderPass() const { return m_vkMainRenderPass; }
	void SetMainRenderPass(VkRenderPass vkRenderPass) { m_vkMainRenderPass = vkRenderPass; }

	[[nodiscard]]
	DescriptorSet& AllocateDescriptorSet(VkDescriptorSetLayout vkSetLayout) const;
	[[nodiscard]]
	inline VkDescriptorSetLayout GetEmptyDescriptorSetLayout() const { return m_vkEmptySetLayout; }

	[[nodiscard]]
	inline u8 ContextIndex() const { return m_ContextIndex; }
	[[nodiscard]]
	inline u32 NumContexts() const { return m_NumContexts; }
	void SetNumContexts(u32 num) { m_NumContexts = num; }

	[[nodiscard]]
	u32 WindowWidth() const { return m_WindowWidth; }
	void SetWindowWidth(u32 width) { m_WindowWidth = width; }
	[[nodiscard]]
	u32 WindowHeight() const { return m_WindowHeight; }
	void SetWindowHeight(u32 height) { m_WindowHeight = height; }

	void SetVkObjectName(std::string_view name, u64 handle, VkObjectType type);

private:
	VkInstance					m_vkInstance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT	m_vkDebugMessenger = VK_NULL_HANDLE;
								
	VkPhysicalDevice			m_vkPhysicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties	m_PhysicalDeviceProperties;
	VkDevice					m_vkDevice = VK_NULL_HANDLE;

	CommandQueue* m_pGraphicsQueue = nullptr;
	CommandQueue* m_pComputeQueue = nullptr;
	CommandQueue* m_pTransferQueue = nullptr;

	VmaAllocator     m_vmaAllocator = VK_NULL_HANDLE;
	ResourceManager* m_pResourceManager = nullptr;

	VkRenderPass m_vkMainRenderPass = VK_NULL_HANDLE;

	VkDescriptorSetLayout m_vkEmptySetLayout = VK_NULL_HANDLE;
	DescriptorPool*       m_pGlobalDescriptorPool = nullptr;

	u32 m_NumContexts = 0;
	u8  m_ContextIndex = 0;

	u32 m_WindowWidth = 0;
	u32 m_WindowHeight = 0;
};

} // namespace vk