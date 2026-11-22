#pragma once
#include "RenderCommon/RenderDevice.h"
#include "FreeList.hpp"

#include <atomic>
#include <bitset>

namespace vk
{

class CommandQueue;
class VkCommandContext;
class VkResourceManager;
class VulkanSampler;

enum class eCommandType;

struct DeviceCapabilities
{
	bool bValidation                = false;
	bool bDebugUtils                = false;
	bool bSurfaceMaintenance        = false;
	bool bMeshShader                = false;
	bool bSwapchainMaintenance      = false;
	bool bMemoryBudget              = false;
	bool bPipelineStatistics        = false;
	u32 graphicsTimestampValidBits  = 0;
	u32 computeTimestampValidBits   = 0;
};

struct DeviceDispatch
{
	PFN_vkCmdDrawMeshTasksIndirectEXT         cmdDrawMeshTasksIndirect = nullptr;
	PFN_vkCmdDrawMeshTasksIndirectCountEXT    cmdDrawMeshTasksIndirectCount = nullptr;
	PFN_vkSetDebugUtilsObjectNameEXT           setDebugUtilsObjectName = nullptr;
	PFN_vkCmdBeginDebugUtilsLabelEXT           cmdBeginDebugUtilsLabel = nullptr;
	PFN_vkCmdEndDebugUtilsLabelEXT             cmdEndDebugUtilsLabel = nullptr;
};

class VkRenderDevice : public render::RenderDevice
{
using Super = render::RenderDevice;
public:
	VkRenderDevice(const render::DeviceSettings& ds);
	~VkRenderDevice();

	virtual void Flush() override;

	virtual Arc< render::Buffer > CreateBuffer(const char* name, render::Buffer::CreationInfo&& desc) override;
	virtual Arc< render::Buffer > CreateEmptyBuffer(const char* name = "") override;
	virtual Arc< render::Texture > CreateTexture(const char* name, render::Texture::CreationInfo&& desc) override;
	virtual Arc< render::Texture > CreateEmptyTexture(const char* name = "") override;

	virtual Arc< render::RenderTarget > CreateEmptyRenderTarget(const char* name = "") override;

	virtual Arc< render::Sampler > CreateSampler(const char* name, render::Sampler::CreationInfo&& info) override;

	virtual Arc< render::Shader > CreateShader(const char* name, render::Shader::CreationInfo&& info) override;
	virtual Arc< render::ShaderBindingTable > CreateSBT(const char* name) override;

	virtual Arc< render::BottomLevelAccelerationStructure > CreateBLAS(const char* name) override;
	virtual Arc< render::TopLevelAccelerationStructure > CreateTLAS(const char* name) override;

	virtual Box< render::ComputePipeline > CreateComputePipeline(const char* name) override;
	virtual Box< render::GraphicsPipeline > CreateGraphicsPipeline(const char* name) override;
	virtual Box< render::RaytracingPipeline > CreateRaytracingPipeline(const char* name) override;

	inline VkInstance vkInstance() const { return m_vkInstance; }
	inline VkDevice vkDevice() const { return m_vkDevice; }
	inline VkPhysicalDevice vkPhysicalDevice() const { return m_vkPhysicalDevice; }
	inline const VkPhysicalDeviceProperties& DeviceProps() const { return m_PhysicalDeviceProperties; }
	inline const VkPhysicalDeviceFeatures& DeviceFeatures() const { return m_PhysicalDeviceFeatures; }
	inline const DeviceCapabilities& Capabilities() const { return m_Capabilities; }
	inline const DeviceDispatch& Dispatch() const { return m_Dispatch; }
	u32 TimestampValidBits(eCommandType cmdType) const;

	inline CommandQueue& GraphicsQueue() const { return *m_pGraphicsQueue; }
	inline CommandQueue& ComputeQueue() const { return *m_pComputeQueue; }
	inline CommandQueue& TransferQueue() const { return *m_pTransferQueue; }

	Arc< VkCommandContext > BeginCommand(eCommandType cmdType, VkCommandBufferUsageFlags usage = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, bool bTransient = false);
	void ExecuteCommand(Arc< VkCommandContext > pContext);

	inline VmaAllocator vmaAllocator() const { return m_vmaAllocator; }
	virtual render::ResourceManager& GetResourceManager() const override;

	virtual bool SaveTextureToEXR(const Arc< render::Texture >& pTexture, const char* path) override;

	virtual u64 ValidationErrorCount() const override;
	// The messenger alone is not enough: the loader can serve VK_EXT_debug_utils without VK_LAYER_KHRONOS_validation.
	virtual bool IsValidationActive() const override { return m_Capabilities.bValidation && m_vkDebugMessenger != VK_NULL_HANDLE; }

	inline VkDescriptorSet GetHeapDescriptorSet() const { return m_vkHeapSet; }
	inline VkDescriptorSet GetSamplerDescriptorSet() const { return m_vkSamplerSet; }
	inline VkPipelineLayout GetGlobalPipelineLayout() const { return m_vkGlobalPipelineLayout; }

	[[nodiscard]]
	u32 AllocateHeapSlot();
	void FreeHeapSlot(u32 slot);
	void WriteHeapImage(u32 slot, VkDescriptorType type, VkImageView vkView, VkImageLayout layout);
	void WriteHeapBuffer(u32 slot, VkDescriptorType type, VkBuffer vkBuffer);

	using render::RenderDevice::DeferRelease;
	void DeferRelease(VkBuffer vkBuffer, VmaAllocation vmaAllocation);
	void DeferRelease(VkImage vkImage, VmaAllocation vmaAllocation);
	void DeferRelease(VkImageView vkImageView);
	void DeferRelease(VkFramebuffer vkFramebuffer);
	void DeferRelease(VkRenderPass vkRenderPass);
	void DeferRelease(VkPipeline vkPipeline);
	void DeferRelease(VkSampler vkSampler);

	void SetVkObjectName(const std::string& name, u64 handle, VkObjectType type);

private:
	void CreateDescriptorHeap();
	void CreateStaticSamplers();
	void CreateGlobalPipelineLayout();

private:
	VkInstance					m_vkInstance       = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT	m_vkDebugMessenger = VK_NULL_HANDLE;
								
	VkPhysicalDevice           m_vkPhysicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties m_PhysicalDeviceProperties;
	VkPhysicalDeviceFeatures   m_PhysicalDeviceFeatures = {};

	VkDevice m_vkDevice = VK_NULL_HANDLE;
	DeviceCapabilities m_Capabilities;
	DeviceDispatch m_Dispatch;

	CommandQueue* m_pGraphicsQueue = nullptr;
	CommandQueue* m_pComputeQueue  = nullptr;
	CommandQueue* m_pTransferQueue = nullptr;

	VmaAllocator       m_vmaAllocator     = VK_NULL_HANDLE;

	VkDescriptorSetLayout m_vkHeapSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool      m_vkHeapPool      = VK_NULL_HANDLE;
	VkDescriptorSet       m_vkHeapSet       = VK_NULL_HANDLE; // freed with m_vkHeapPool

	baamboo::FreeList< u32 >            m_HeapSlots;
	std::bitset< kHeapDescriptorCount > m_HeapSlotsInUse;

	std::array< Arc< VulkanSampler >, static_cast< size_t >(render::eSamplerIndex::MaxIndex) > m_pStaticSamplers;
	VkDescriptorSetLayout m_vkSamplerSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool      m_vkSamplerPool      = VK_NULL_HANDLE;
	VkDescriptorSet       m_vkSamplerSet       = VK_NULL_HANDLE; // freed with m_vkSamplerPool

	VkPipelineLayout m_vkGlobalPipelineLayout = VK_NULL_HANDLE;

	VkResourceManager* m_pResourceManager = nullptr;

	u64 m_ValidationErrorBase = 0;
};

} // namespace vk