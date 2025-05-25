#include "RendererPch.h"
#include "VkRenderContext.h"
#include "VkBuildHelpers.h"
#include "VkCommandQueue.h"
#include "VkCommandBuffer.h"
#include "VkResourceManager.h"
#include "VkDescriptorPool.h"

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
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_DeviceAddress)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_SamplerAnistropy)
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
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
	allocatorInfo.physicalDevice = m_vkPhysicalDevice;
	allocatorInfo.device = m_vkDevice;
	allocatorInfo.instance = m_vkInstance;
	allocatorInfo.pVulkanFunctions = nullptr;
	vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator);

	m_pResourceManager = new ResourceManager(*this);


	// **
	// Descriptor pool
	// **
	std::vector< VkDescriptorPoolSize > poolSizes = 
	{
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32 },
	};
	m_pGlobalDescriptorPool = new DescriptorPool(*this, std::move(poolSizes), 1024);

	VkDescriptorSetLayoutCreateInfo emptyLayoutInfo = {};
	emptyLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	emptyLayoutInfo.bindingCount = 0;
	emptyLayoutInfo.pBindings = nullptr;
	VK_CHECK(vkCreateDescriptorSetLayout(m_vkDevice, &emptyLayoutInfo, nullptr, &m_vkEmptySetLayout));

	g_FrameData.pSceneResource = new SceneResource(*this);
}

RenderContext::~RenderContext()
{
	RELEASE(g_FrameData.pSceneResource);

	RELEASE(m_pTransferQueue);
	RELEASE(m_pComputeQueue);
	RELEASE(m_pGraphicsQueue);

	RELEASE(m_pResourceManager);
	RELEASE(m_pGlobalDescriptorPool);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_vkEmptySetLayout, nullptr);
	vmaDestroyAllocator(m_vmaAllocator);

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

DescriptorSet& RenderContext::AllocateDescriptorSet(VkDescriptorSetLayout vkSetLayout) const
{
	return m_pGlobalDescriptorPool->AllocateSet(vkSetLayout);
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