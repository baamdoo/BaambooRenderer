#include "RendererPch.h"
#include "VkRenderContext.h"
#include "VkBuildHelpers.h"
#include "VkCommandQueue.h"
#include "VkResourceManager.h"

namespace vk
{

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
{
	UNUSED(severity); UNUSED(type); UNUSED(userData);

	printf("Validation layer: %s\n", callbackData->pMessage);
	return VK_FALSE;
}

RenderContext::RenderContext()
{
	InstanceBuilder instanceBuilder;
	m_vkInstance = instanceBuilder.AddExtensionLayer(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME).Build();

	DebugMessengerBuilder debugMessengerBuilder;
	m_vkDebugMessenger = debugMessengerBuilder.SetDebugMessageCallback(DebugCallback).Build(m_vkInstance);

	DeviceBuilder deviceBuilder;
	m_vkDevice = deviceBuilder.AddDeviceExtension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)
		                      .AddDeviceExtension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_IndirectRendering)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_DescriptorIndexing)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_DynamicIndexing)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_IndexTypeUint8)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_Sync2).Build(m_vkInstance);
	m_vkPhysicalDevice = deviceBuilder.physicalDevice;
	m_physicalDeviceProperties = deviceBuilder.physicalDeviceProperties;

	m_pGraphicsQueue = new CommandQueue(*this, deviceBuilder.queueFamilyIndices.graphicsQueueIndex);
	m_pComputeQueue = new CommandQueue(*this, deviceBuilder.queueFamilyIndices.computeQueueIndex);
	m_pTransferQueue = new CommandQueue(*this, deviceBuilder.queueFamilyIndices.transferQueueIndex);
	assert(m_pGraphicsQueue && m_pComputeQueue);


	// **
	// Resource management
	// **
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
	allocatorInfo.physicalDevice = m_vkPhysicalDevice;
	allocatorInfo.device = m_vkDevice;
	allocatorInfo.instance = m_vkInstance;
	allocatorInfo.pVulkanFunctions = nullptr;
	vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator);

	m_pResourceManager = new ResourceManager(*this);


	// **
	// Bindless descriptor pool
	// **
	std::vector< VkDescriptorPoolSize > poolSizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_physicalDeviceProperties.limits.maxDescriptorSetStorageBuffers },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_physicalDeviceProperties.limits.maxDescriptorSetSampledImages },
	};
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	descriptorPoolInfo.maxSets = 1;
	descriptorPoolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
	descriptorPoolInfo.pPoolSizes = poolSizes.data();
	VK_CHECK(vkCreateDescriptorPool(m_vkDevice, &descriptorPoolInfo, nullptr, &m_vkStaticDescriptorPool));
}

RenderContext::~RenderContext()
{
	vkDestroyDescriptorPool(m_vkDevice, m_vkStaticDescriptorPool, nullptr);

	RELEASE(m_pResourceManager);
	vmaDestroyAllocator(m_vmaAllocator);

	RELEASE(m_pTransferQueue);
	RELEASE(m_pComputeQueue);
	RELEASE(m_pGraphicsQueue);

	vkDestroyDevice(m_vkDevice, nullptr);

	auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_vkInstance, "vkDestroyDebugUtilsMessengerEXT");
	vkDestroyDebugUtilsMessengerEXT(m_vkInstance, m_vkDebugMessenger, nullptr);
	vkDestroyInstance(m_vkInstance, nullptr);
}

u8 RenderContext::NextFrame()
{
	m_contextIndex = (m_contextIndex + 1) % m_numContexts;
	return m_contextIndex;
}

VkDeviceSize RenderContext::GetAlignedSize(VkDeviceSize size) const
{
	const VkDeviceSize alignment = m_physicalDeviceProperties.limits.minMemoryMapAlignment;
	return (size + alignment - 1) & ~(alignment - 1);
}

void RenderContext::SetVkObjectName(std::string_view name, u64 handle, VkObjectType type)
{
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = type;
	nameInfo.objectHandle = handle;
	nameInfo.pObjectName = name.data();

	auto vkSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(m_vkInstance, "vkSetDebugUtilsObjectNameEXT");
	VK_CHECK(vkSetDebugUtilsObjectNameEXT(m_vkDevice, &nameInfo));
}

} // namespace vk