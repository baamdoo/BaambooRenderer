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

VkRenderDevice::VkRenderDevice(const render::DeviceSettings& ds)
	: Super(ds)
{
	BB_ASSERT(ds.bMeshShader,
		"The Vulkan backend currently requires the mesh-shader path; a complete vertex fallback is not available.");

	BB_ASSERT(!ds.bRaytracing,
		"The Vulkan backend does not currently implement ray tracing. Select the D3D12 backend for ray-tracing applications.");

	InstanceBuilder instanceBuilder;
	m_vkInstance = instanceBuilder
		.SetValidationFeatureEnable({ VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT })
		.Build();

	m_Capabilities.bValidation         = instanceBuilder.bValidationEnabled;
	m_Capabilities.bValidationFeatures = instanceBuilder.bValidationFeaturesEnabled;
	m_Capabilities.bDebugUtils         = instanceBuilder.bDebugUtilsEnabled;
	m_Capabilities.bSurfaceMaintenance = instanceBuilder.bSurfaceMaintenanceEnabled;

	if (m_Capabilities.bDebugUtils)
	{
		DebugMessengerBuilder debugMessengerBuilder;
		m_vkDebugMessenger = debugMessengerBuilder.SetDebugMessageCallback(DebugCallback).Build(m_vkInstance);
	}

	DeviceBuilder deviceBuilder;
	m_vkDevice = deviceBuilder
		.RequireMeshShader(true)
		.EnableSwapchainMaintenance(m_Capabilities.bSurfaceMaintenance)
		.Build(m_vkInstance);

	m_vkPhysicalDevice = deviceBuilder.physicalDevice;
	m_PhysicalDeviceProperties = deviceBuilder.physicalDeviceProperties;
	m_PhysicalDeviceMaintenance3Properties = deviceBuilder.physicalDeviceMaintenance3Properties;
	m_PhysicalDeviceFeatures = deviceBuilder.enabledPhysicalDeviceFeatures;

	m_Capabilities.bMeshShader           = deviceBuilder.bMeshShaderEnabled;
	m_Capabilities.bSwapchainMaintenance = deviceBuilder.bSwapchainMaintenanceEnabled;
	m_Capabilities.bMemoryBudget              = deviceBuilder.bMemoryBudgetEnabled;
	m_Capabilities.bPipelineStatistics        = deviceBuilder.bPipelineStatisticsEnabled;
	m_Capabilities.graphicsTimestampValidBits = deviceBuilder.queueFamilyIndices.graphicsTimestampValidBits;
	m_Capabilities.computeTimestampValidBits  = deviceBuilder.queueFamilyIndices.computeTimestampValidBits;
	m_Settings.bMeshShader = m_Capabilities.bMeshShader;

	m_Dispatch.cmdPushDescriptorSet = reinterpret_cast< PFN_vkCmdPushDescriptorSetKHR >(
		vkGetDeviceProcAddr(m_vkDevice, "vkCmdPushDescriptorSetKHR"));
	BB_ASSERT(m_Dispatch.cmdPushDescriptorSet != nullptr,
		"Required Vulkan push-descriptor command is unavailable after device creation.");

	if (m_Capabilities.bMeshShader)
	{
		m_Dispatch.cmdDrawMeshTasksIndirect = reinterpret_cast< PFN_vkCmdDrawMeshTasksIndirectEXT >(
			vkGetDeviceProcAddr(m_vkDevice, "vkCmdDrawMeshTasksIndirectEXT"));
		m_Dispatch.cmdDrawMeshTasksIndirectCount = reinterpret_cast< PFN_vkCmdDrawMeshTasksIndirectCountEXT >(
			vkGetDeviceProcAddr(m_vkDevice, "vkCmdDrawMeshTasksIndirectCountEXT"));
		BB_ASSERT(m_Dispatch.cmdDrawMeshTasksIndirect != nullptr &&
			m_Dispatch.cmdDrawMeshTasksIndirectCount != nullptr,
			"Required Vulkan mesh-shader commands are unavailable after device creation.");
	}

	if (m_Capabilities.bDebugUtils)
	{
		m_Dispatch.setDebugUtilsObjectName = reinterpret_cast< PFN_vkSetDebugUtilsObjectNameEXT >(
			vkGetDeviceProcAddr(m_vkDevice, "vkSetDebugUtilsObjectNameEXT"));
		m_Dispatch.cmdBeginDebugUtilsLabel = reinterpret_cast< PFN_vkCmdBeginDebugUtilsLabelEXT >(
			vkGetDeviceProcAddr(m_vkDevice, "vkCmdBeginDebugUtilsLabelEXT"));
		m_Dispatch.cmdEndDebugUtilsLabel = reinterpret_cast< PFN_vkCmdEndDebugUtilsLabelEXT >(
			vkGetDeviceProcAddr(m_vkDevice, "vkCmdEndDebugUtilsLabelEXT"));
	}

	m_pGraphicsQueue = new CommandQueue(*this, deviceBuilder.queueFamilyIndices.graphicsQueueIndex, eCommandType::Graphics);
	m_pComputeQueue  = new CommandQueue(*this, deviceBuilder.queueFamilyIndices.computeQueueIndex, eCommandType::Compute);
	m_pTransferQueue = new CommandQueue(*this, deviceBuilder.queueFamilyIndices.transferQueueIndex, eCommandType::Transfer);
	assert(m_pGraphicsQueue && m_pComputeQueue && m_pTransferQueue);


	// **
	// Resource management
	// **
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	if (m_Capabilities.bMemoryBudget)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
	allocatorInfo.physicalDevice   = m_vkPhysicalDevice;
	allocatorInfo.device           = m_vkDevice;
	allocatorInfo.instance         = m_vkInstance;
	allocatorInfo.pVulkanFunctions = nullptr;
	const VkResult allocatorResult = vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator);
	BB_ASSERT(allocatorResult == VK_SUCCESS, "Failed to create Vulkan memory allocator (%d).", static_cast<i32>(allocatorResult));

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
	if (m_vmaAllocator)
		vmaDestroyAllocator(m_vmaAllocator);

	vkDestroyDevice(m_vkDevice, nullptr);

	if (m_vkDebugMessenger)
	{
		const auto destroyDebugMessenger = reinterpret_cast< PFN_vkDestroyDebugUtilsMessengerEXT >(
			vkGetInstanceProcAddr(m_vkInstance, "vkDestroyDebugUtilsMessengerEXT"));
		if (destroyDebugMessenger)
			destroyDebugMessenger(m_vkInstance, m_vkDebugMessenger, nullptr);
	}

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

Arc< render::ShaderBindingTable > VkRenderDevice::CreateSBT(const char* name)
{
	UNUSED(name);
	BB_ASSERT(false, "Vulkan shader binding tables are unavailable because Vulkan ray tracing is unsupported.");
	return {};
}

Arc< render::BottomLevelAccelerationStructure > VkRenderDevice::CreateBLAS(const char* name)
{
	UNUSED(name);
	BB_ASSERT(false, "Vulkan BLAS creation is unavailable because Vulkan ray tracing is unsupported.");
	return {};
}

Arc< render::TopLevelAccelerationStructure > VkRenderDevice::CreateTLAS(const char* name)
{
	UNUSED(name);
	BB_ASSERT(false, "Vulkan TLAS creation is unavailable because Vulkan ray tracing is unsupported.");
	return {};
}

Box< render::ComputePipeline > VkRenderDevice::CreateComputePipeline(const char* name)
{
	return MakeBox< VulkanComputePipeline >(*this, name);
}

Box< render::GraphicsPipeline > VkRenderDevice::CreateGraphicsPipeline(const char* name)
{
	return MakeBox< VulkanGraphicsPipeline >(*this, name);
}

Box< render::RaytracingPipeline > VkRenderDevice::CreateRaytracingPipeline(const char* name)
{
	UNUSED(name);
	BB_ASSERT(false, "Vulkan ray-tracing pipelines are unsupported.");
	return {};
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


u32 VkRenderDevice::TimestampValidBits(eCommandType cmdType) const
{
	switch (cmdType)
	{
	case eCommandType::Graphics:
		return m_Capabilities.graphicsTimestampValidBits;
	case eCommandType::Compute:
		return m_Capabilities.computeTimestampValidBits;
	case eCommandType::Transfer:
		return 0;
	default:
		BB_ASSERT(false, "Invalid Vulkan command type.");
		return 0;
	}
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

bool VkRenderDevice::SaveTextureToEXR(const Arc< render::Texture >& pTexture, const char* path)
{
	(void)pTexture;
	return false;
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

#ifdef _DEBUG
	if (m_Dispatch.setDebugUtilsObjectName)
	{
		const VkResult result = m_Dispatch.setDebugUtilsObjectName(m_vkDevice, &nameInfo);
		BB_ASSERT(result == VK_SUCCESS, "Failed to set Vulkan object name (%d).", static_cast<i32>(result));
	}
#endif
}

} // namespace vk