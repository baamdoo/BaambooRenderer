#include "RendererPch.h"
#include "VkRenderDevice.h"
#include "VkBuildHelpers.h"
#include "VkCommandQueue.h"
#include "VkCommandContext.h"
#include "VkResourceManager.h"
#include "VkDescriptorPool.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSceneResource.h"

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

VkRenderDevice::VkRenderDevice()
{
	InstanceBuilder instanceBuilder;
	m_vkInstance = instanceBuilder.AddExtensionLayer(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME).Build();

	DebugMessengerBuilder debugMessengerBuilder;
	m_vkDebugMessenger = debugMessengerBuilder.SetDebugMessageCallback(DebugCallback).Build(m_vkInstance);

	DeviceBuilder deviceBuilder;
	m_vkDevice = deviceBuilder.AddDeviceExtension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)
		                      .AddDeviceExtension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
		                      .AddDeviceExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_IndirectRendering)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_DescriptorIndexing)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_DynamicIndexing)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_DynamicRendering)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_ShaderInt64)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_DeviceAddress)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_SamplerAnistropy)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_IndexTypeUint8)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_Sync2)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_MeshShader)
		                      .AddPhysicalDeviceFeature(ePhysicalDeviceFeature_SwapChainMaintenance).Build(m_vkInstance);
	m_vkPhysicalDevice         = deviceBuilder.physicalDevice;
	m_PhysicalDeviceProperties = deviceBuilder.physicalDeviceProperties;
	m_PhysicalDeviceMaintenance3Properties = deviceBuilder.physicalDeviceMaintenance3Properties;

	m_pGraphicsQueue = new CommandQueue(*this, deviceBuilder.queueFamilyIndices.graphicsQueueIndex, eCommandType::Graphics);
	m_pComputeQueue  = new CommandQueue(*this, deviceBuilder.queueFamilyIndices.computeQueueIndex, eCommandType::Compute);
	m_pTransferQueue = new CommandQueue(*this, deviceBuilder.queueFamilyIndices.transferQueueIndex, eCommandType::Transfer);
	assert(m_pGraphicsQueue && m_pComputeQueue && m_pTransferQueue);


	// **
	// Resource management
	// **
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.flags            = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
	allocatorInfo.physicalDevice   = m_vkPhysicalDevice;
	allocatorInfo.device           = m_vkDevice;
	allocatorInfo.instance         = m_vkInstance;
	allocatorInfo.pVulkanFunctions = nullptr;
	vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator);

	m_pResourceManager = new VkResourceManager(*this);


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
	emptyLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	emptyLayoutInfo.bindingCount = 0;
	emptyLayoutInfo.pBindings    = nullptr;
	VK_CHECK(vkCreateDescriptorSetLayout(m_vkDevice, &emptyLayoutInfo, nullptr, &m_vkEmptySetLayout));
}

VkRenderDevice::~VkRenderDevice()
{
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

u32 VkRenderDevice::NextFrame()
{
	m_ContextIndex = (m_ContextIndex + 1) % m_NumContexts;
	return m_ContextIndex;
}

void VkRenderDevice::Flush()
{
	m_pGraphicsQueue->Flush();
	m_pComputeQueue->Flush();
	m_pTransferQueue->Flush();
}

Arc< render::Buffer > VkRenderDevice::CreateBuffer(const char* name, render::Buffer::CreationInfo&& desc)
{
	return VulkanBuffer::Create(*this, name, std::move(desc));
}

Arc<render::Buffer> VkRenderDevice::CreateEmptyBuffer(const char* name)
{
	return VulkanBuffer::CreateEmpty(*this, name);
}

Arc< render::Texture > VkRenderDevice::CreateTexture(const char* name, render::Texture::CreationInfo&& desc)
{
	return VulkanTexture::Create(*this, name, std::move(desc));
}

Arc< render::Texture > VkRenderDevice::CreateEmptyTexture(const char* name)
{
	return VulkanTexture::CreateEmpty(*this, name);
}

Arc< render::RenderTarget > VkRenderDevice::CreateEmptyRenderTarget(const char* name)
{
	return MakeArc< VulkanRenderTarget >(*this, name);
}

Arc< render::Sampler > VkRenderDevice::CreateSampler(const char* name, render::Sampler::CreationInfo&& info)
{
	return VulkanSampler::Create(*this, name, std::move(info));
}

Arc< render::Shader > VkRenderDevice::CreateShader(const char* name, render::Shader::CreationInfo&& info)
{
	return VulkanShader::Create(*this, name, std::move(info));
}

Box< render::ComputePipeline > VkRenderDevice::CreateComputePipeline(const char* name)
{
	return MakeBox< VulkanComputePipeline >(*this, name);
}

Box< render::GraphicsPipeline > VkRenderDevice::CreateGraphicsPipeline(const char* name)
{
	return MakeBox< VulkanGraphicsPipeline >(*this, name);
}

Box< render::SceneResource > VkRenderDevice::CreateSceneResource()
{
	return MakeBox< VkSceneResource >(*this);
}

VkDeviceSize VkRenderDevice::GetAlignedSize(VkDeviceSize size) const
{
	const VkDeviceSize alignment = m_PhysicalDeviceProperties.limits.minMemoryMapAlignment;
	return (size + alignment - 1) & ~(alignment - 1);
}

CommandQueue& VkRenderDevice::GetQueue(eCommandType cmdType) const
{
	switch (cmdType)
	{
	case eCommandType::Graphics:
		return GraphicsQueue();
	case eCommandType::Compute:
		return ComputeQueue();
	case eCommandType::Transfer:
		return TransferQueue();

	default:
		assert(false && "Invalid Command Type!"); break;
	}

	return GraphicsQueue();
}

Arc< VkCommandContext > VkRenderDevice::BeginCommand(eCommandType cmdType, VkCommandBufferUsageFlags usage, bool bTransient)
{
	switch (cmdType)
	{
	case eCommandType::Graphics:
		return m_pGraphicsQueue->Allocate(usage, bTransient);
	case eCommandType::Compute:
		return m_pComputeQueue->Allocate(usage, bTransient);
	case eCommandType::Transfer:
		return m_pTransferQueue->Allocate(usage, bTransient);

	default:
		assert(false && "Invalid entry!"); break;
	}

	return nullptr;
}

void VkRenderDevice::ExecuteCommand(Arc< VkCommandContext > pContext)
{
	switch (pContext->GetCommandType())
	{
	case eCommandType::Graphics:
		GraphicsQueue().ExecuteCommandBuffer(pContext);
		break;
	case eCommandType::Compute:
		ComputeQueue().ExecuteCommandBuffer(pContext);
		break;
	case eCommandType::Transfer:
		TransferQueue().ExecuteCommandBuffer(pContext);
		break;
	}
}

render::ResourceManager& VkRenderDevice::GetResourceManager() const
{
	return *m_pResourceManager;
}

DescriptorSet& VkRenderDevice::AllocateDescriptorSet(VkDescriptorSetLayout vkSetLayout) const
{
	return m_pGlobalDescriptorPool->AllocateSet(vkSetLayout);
}

void VkRenderDevice::SetVkObjectName(const std::string& name, u64 handle, VkObjectType type)
{
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType   = type;
	nameInfo.objectHandle = handle;
	nameInfo.pObjectName  = name.data();

	static auto vkSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(m_vkInstance, "vkSetDebugUtilsObjectNameEXT");
	VK_CHECK(vkSetDebugUtilsObjectNameEXT(m_vkDevice, &nameInfo));
}

} // namespace vk