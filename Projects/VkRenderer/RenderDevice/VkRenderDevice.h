#pragma once
#include "RenderCommon/RenderDevice.h"

namespace vk
{

class CommandQueue;
class VkCommandContext;
class VkResourceManager;
class DescriptorSet;
class DescriptorPool;

enum class eCommandType;

class VkRenderDevice : public render::RenderDevice
{
public:
	VkRenderDevice();
	~VkRenderDevice();

	u32 NextFrame();

	virtual void Flush() override;

	virtual Arc< render::Buffer > CreateBuffer(const std::string& name, render::Buffer::CreationInfo&& desc) override;
	virtual Arc< render::Texture > CreateTexture(const std::string& name, render::Texture::CreationInfo&& desc) override;
	virtual Arc< render::Texture > CreateEmptyTexture(const std::string& name = "") override;

	virtual Arc< render::RenderTarget > CreateEmptyRenderTarget(const std::string& name = "") override;

	virtual Arc< render::Sampler > CreateSampler(const std::string& name, render::Sampler::CreationInfo&& info) override;

	virtual Arc< render::Shader > CreateShader(const std::string& name, render::Shader::CreationInfo&& info) override;

	virtual Box< render::ComputePipeline > CreateComputePipeline(const std::string& name) override;
	virtual Box< render::GraphicsPipeline > CreateGraphicsPipeline(const std::string& name) override;

	virtual Box< render::SceneResource > CreateSceneResource() override;

	VkDeviceSize GetAlignedSize(VkDeviceSize size) const;

	inline VkInstance vkInstance() const { return m_vkInstance; }
	inline VkDevice vkDevice() const { return m_vkDevice; }
	inline VkPhysicalDevice vkPhysicalDevice() const { return m_vkPhysicalDevice; }
	inline const VkPhysicalDeviceProperties& DeviceProps() const { return m_PhysicalDeviceProperties; }

	CommandQueue& GetQueue(eCommandType cmdType) const;
	inline CommandQueue& GraphicsQueue() const { return *m_pGraphicsQueue; }
	inline CommandQueue& ComputeQueue() const { return *m_pComputeQueue; }
	inline CommandQueue& TransferQueue() const { return *m_pTransferQueue; }

	Arc< VkCommandContext > BeginCommand(eCommandType cmdType, VkCommandBufferUsageFlags usage = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, bool bTransient = false);
	void ExecuteCommand(Arc< VkCommandContext > pContext);

	inline VmaAllocator vmaAllocator() const { return m_vmaAllocator; }
	virtual render::ResourceManager& GetResourceManager() const override;

	inline VkRenderPass vkMainRenderPass() const { return m_vkMainRenderPass; }
	void SetMainRenderPass(VkRenderPass vkRenderPass) { m_vkMainRenderPass = vkRenderPass; }

	DescriptorSet& AllocateDescriptorSet(VkDescriptorSetLayout vkSetLayout) const;
	inline VkDescriptorSetLayout GetEmptyDescriptorSetLayout() const { return m_vkEmptySetLayout; }

	void SetVkObjectName(const std::string& name, u64 handle, VkObjectType type);

private:
	VkInstance					m_vkInstance       = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT	m_vkDebugMessenger = VK_NULL_HANDLE;
								
	VkPhysicalDevice			m_vkPhysicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties	m_PhysicalDeviceProperties;
	VkDevice					m_vkDevice = VK_NULL_HANDLE;

	CommandQueue* m_pGraphicsQueue = nullptr;
	CommandQueue* m_pComputeQueue  = nullptr;
	CommandQueue* m_pTransferQueue = nullptr;

	VmaAllocator       m_vmaAllocator     = VK_NULL_HANDLE;
	VkResourceManager* m_pResourceManager = nullptr;

	VkRenderPass m_vkMainRenderPass = VK_NULL_HANDLE;

	VkDescriptorSetLayout m_vkEmptySetLayout      = VK_NULL_HANDLE;
	DescriptorPool*       m_pGlobalDescriptorPool = nullptr;
};

} // namespace vk