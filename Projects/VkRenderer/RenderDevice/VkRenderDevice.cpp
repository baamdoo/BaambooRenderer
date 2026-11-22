#include "RendererPch.h"
#include "VkRenderDevice.h"
#include "VkBuildHelpers.h"
#include "VkCommandQueue.h"
#include "VkCommandContext.h"
#include "VkResourceManager.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSampler.h"
#include "RenderResource/VkBuffer.h"

namespace vk
{

static constexpr VkDescriptorType kHeapDescriptorTypes[] =
{
	VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
};

struct StaticSamplerDesc
{
	const char*                   name;
	render::Sampler::CreationInfo info;
};

static StaticSamplerDesc GetStaticSamplerDesc(render::eSamplerIndex index)
{
	using namespace render;
	switch (index)
	{
	case eSamplerIndex::LinearClamp:
		return { "LinearClamp", { .filter = eFilterMode::Linear, .mipmapMode = eMipmapMode::Nearest, .addressMode = eAddressMode::ClampEdge } };
	case eSamplerIndex::LinearWrap:
		return { "LinearWrap", { .filter = eFilterMode::Linear, .mipmapMode = eMipmapMode::Nearest, .addressMode = eAddressMode::Wrap } };
	case eSamplerIndex::PointClamp:
		return { "PointClamp", { .filter = eFilterMode::Point, .mipmapMode = eMipmapMode::Nearest, .addressMode = eAddressMode::ClampEdge } };
	case eSamplerIndex::PointWrap:
		return { "PointWrap", { .filter = eFilterMode::Point, .mipmapMode = eMipmapMode::Nearest, .addressMode = eAddressMode::Wrap } };
	case eSamplerIndex::TrilinearWrap:
		return { "TrilinearWrap", { .filter = eFilterMode::Linear, .mipmapMode = eMipmapMode::Linear, .addressMode = eAddressMode::Wrap } };
	case eSamplerIndex::AnisotropicWrap:
		return { "AnisotropicWrap", { .filter = eFilterMode::Anisotropic, .mipmapMode = eMipmapMode::Linear, .addressMode = eAddressMode::Wrap,
			.maxAnisotropy = 16.0f } };
	case eSamplerIndex::ShadowCmpLessEqual:
		return { "ShadowCmpLessEqual", { .filter = eFilterMode::Linear, .mipmapMode = eMipmapMode::Nearest, .addressMode = eAddressMode::ClampBorder,
			.compareOp = eCompareOp::LessEqual, .borderColor = eBorderColor::OpaqueWhite_Float } }; // depth 1 outside the map
	case eSamplerIndex::PointClampMin:
		return { "PointClampMin", { .filter = eFilterMode::Point, .mipmapMode = eMipmapMode::Nearest, .addressMode = eAddressMode::ClampEdge,
			.reductionMode = eReductionMode::Min } };
	case eSamplerIndex::LinearClampMin:
		return { "LinearClampMin", { .filter = eFilterMode::Linear, .mipmapMode = eMipmapMode::Linear, .addressMode = eAddressMode::ClampEdge,
			.reductionMode = eReductionMode::Min } };
	default:
		BB_ASSERT(false, "Unknown static sampler index %u.", static_cast< u32 >(index));
		return {};
	}
}

static std::atomic< u64 > s_ValidationErrorCount = 0;

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
{
	UNUSED(type); UNUSED(userData);

	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		s_ValidationErrorCount.fetch_add(1, std::memory_order_relaxed);
		fprintf(stderr, "Vulkan validation error: %s\n", callbackData->pMessage);
		return VK_FALSE;
	}

	printf("Validation layer: %s\n", callbackData->pMessage);
	return VK_FALSE;
}

VkRenderDevice::VkRenderDevice(const render::DeviceSettings& ds)
	: Super(ds)
	, m_ValidationErrorBase(s_ValidationErrorCount.load(std::memory_order_relaxed))
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
	m_PhysicalDeviceFeatures = deviceBuilder.enabledPhysicalDeviceFeatures;

	m_Capabilities.bMeshShader           = deviceBuilder.bMeshShaderEnabled;
	m_Capabilities.bSwapchainMaintenance = deviceBuilder.bSwapchainMaintenanceEnabled;
	m_Capabilities.bMemoryBudget              = deviceBuilder.bMemoryBudgetEnabled;
	m_Capabilities.bPipelineStatistics        = deviceBuilder.bPipelineStatisticsEnabled;
	m_Capabilities.graphicsTimestampValidBits = deviceBuilder.queueFamilyIndices.graphicsTimestampValidBits;
	m_Capabilities.computeTimestampValidBits  = deviceBuilder.queueFamilyIndices.computeTimestampValidBits;
	m_Settings.bMeshShader = m_Capabilities.bMeshShader;

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

	CreateDescriptorHeap();
	CreateStaticSamplers();
	CreateGlobalPipelineLayout();

	m_pResourceManager = new VkResourceManager(*this);
}

VkRenderDevice::~VkRenderDevice()
{
	RELEASE(m_pTransferQueue);
	RELEASE(m_pComputeQueue);
	RELEASE(m_pGraphicsQueue);

	RELEASE(m_pResourceManager);
	vkDestroyPipelineLayout(m_vkDevice, m_vkGlobalPipelineLayout, nullptr);

	vkDestroyDescriptorPool(m_vkDevice, m_vkSamplerPool, nullptr); // frees m_vkSamplerSet
	vkDestroyDescriptorSetLayout(m_vkDevice, m_vkSamplerSetLayout, nullptr);
	m_pStaticSamplers = {};
	FlushDeferredReleases();

	BB_ASSERT(m_HeapSlotsInUse.none(), "%zu descriptor heap slots are still in use at device destruction.", m_HeapSlotsInUse.count());
	vkDestroyDescriptorPool(m_vkDevice, m_vkHeapPool, nullptr); // frees m_vkHeapSet
	vkDestroyDescriptorSetLayout(m_vkDevice, m_vkHeapSetLayout, nullptr);
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

u64 VkRenderDevice::ValidationErrorCount() const
{
	return s_ValidationErrorCount.load(std::memory_order_relaxed) - m_ValidationErrorBase;
}

u32 VkRenderDevice::AllocateHeapSlot()
{
	const u32 slot = m_HeapSlots.allocate();
	BB_ASSERT(slot < kHeapDescriptorCount, "The descriptor heap is full (%u slots).", kHeapDescriptorCount);
	m_HeapSlotsInUse.set(slot);
	return slot;
}

void VkRenderDevice::FreeHeapSlot(u32 slot)
{
	BB_ASSERT(slot < kHeapDescriptorCount && m_HeapSlotsInUse.test(slot),
		"Descriptor heap slot %u is freed twice or was never allocated.", slot);

	// frames in flight may still index this slot
	DeferRelease([this, slot]
	{
		m_HeapSlotsInUse.reset(slot);
		m_HeapSlots.release(slot);
	});
}

void VkRenderDevice::DeferRelease(VkBuffer vkBuffer, VmaAllocation vmaAllocation)
{
	DeferRelease([this, vkBuffer, vmaAllocation] { vmaDestroyBuffer(m_vmaAllocator, vkBuffer, vmaAllocation); });
}

void VkRenderDevice::DeferRelease(VkImage vkImage, VmaAllocation vmaAllocation)
{
	DeferRelease([this, vkImage, vmaAllocation] { vmaDestroyImage(m_vmaAllocator, vkImage, vmaAllocation); });
}

void VkRenderDevice::DeferRelease(VkImageView vkImageView)
{
	DeferRelease([this, vkImageView] { vkDestroyImageView(m_vkDevice, vkImageView, nullptr); });
}

void VkRenderDevice::DeferRelease(VkFramebuffer vkFramebuffer)
{
	DeferRelease([this, vkFramebuffer] { vkDestroyFramebuffer(m_vkDevice, vkFramebuffer, nullptr); });
}

void VkRenderDevice::DeferRelease(VkRenderPass vkRenderPass)
{
	DeferRelease([this, vkRenderPass] { vkDestroyRenderPass(m_vkDevice, vkRenderPass, nullptr); });
}

void VkRenderDevice::DeferRelease(VkPipeline vkPipeline)
{
	DeferRelease([this, vkPipeline] { vkDestroyPipeline(m_vkDevice, vkPipeline, nullptr); });
}

void VkRenderDevice::DeferRelease(VkSampler vkSampler)
{
	DeferRelease([this, vkSampler] { vkDestroySampler(m_vkDevice, vkSampler, nullptr); });
}

void VkRenderDevice::WriteHeapImage(u32 slot, VkDescriptorType type, VkImageView vkView, VkImageLayout layout)
{
	BB_ASSERT(slot < kHeapDescriptorCount && m_HeapSlotsInUse.test(slot), "Descriptor heap slot %u is written but not allocated.", slot);

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageView   = vkView;
	imageInfo.imageLayout = layout;

	VkWriteDescriptorSet write = {};
	write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet          = m_vkHeapSet;
	write.dstBinding      = kHeapBinding;
	write.dstArrayElement = slot;
	write.descriptorCount = 1;
	write.descriptorType  = type;
	write.pImageInfo      = &imageInfo;
	vkUpdateDescriptorSets(m_vkDevice, 1, &write, 0, nullptr);
}

void VkRenderDevice::WriteHeapBuffer(u32 slot, VkDescriptorType type, VkBuffer vkBuffer)
{
	BB_ASSERT(slot < kHeapDescriptorCount && m_HeapSlotsInUse.test(slot), "Descriptor heap slot %u is written but not allocated.", slot);

	VkDescriptorBufferInfo bufferInfo = {};
	bufferInfo.buffer = vkBuffer;
	bufferInfo.offset = 0;
	bufferInfo.range  = VK_WHOLE_SIZE;

	VkWriteDescriptorSet write = {};
	write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet          = m_vkHeapSet;
	write.dstBinding      = kHeapBinding;
	write.dstArrayElement = slot;
	write.descriptorCount = 1;
	write.descriptorType  = type;
	write.pBufferInfo     = &bufferInfo;
	vkUpdateDescriptorSets(m_vkDevice, 1, &write, 0, nullptr);
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

// One set with one binding of kHeapDescriptorCount mutable descriptors: the ResourceDescriptorHeap.
void VkRenderDevice::CreateDescriptorHeap()
{
	VkMutableDescriptorTypeListEXT typeList = {};
	typeList.descriptorTypeCount = static_cast< u32 >(std::size(kHeapDescriptorTypes));
	typeList.pDescriptorTypes    = kHeapDescriptorTypes;

	VkMutableDescriptorTypeListEXT layoutTypeLists[kHeapBinding + 1];
	std::fill(std::begin(layoutTypeLists), std::end(layoutTypeLists), typeList);

	VkMutableDescriptorTypeCreateInfoEXT layoutMutableInfo = {};
	layoutMutableInfo.sType                          = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT;
	layoutMutableInfo.mutableDescriptorTypeListCount = static_cast< u32 >(std::size(layoutTypeLists));
	layoutMutableInfo.pMutableDescriptorTypeLists    = layoutTypeLists;

	constexpr VkDescriptorBindingFlags kHeapBindingFlags =
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |            // slots no shader reads may stay unwritten
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |          // slots may be written after the set is bound
		VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT; // slots in-flight work does not read may be written

	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {};
	bindingFlagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	bindingFlagsInfo.pNext         = &layoutMutableInfo;
	bindingFlagsInfo.bindingCount  = 1;
	bindingFlagsInfo.pBindingFlags = &kHeapBindingFlags;

	VkDescriptorSetLayoutBinding binding = {};
	binding.binding         = kHeapBinding;
	binding.descriptorType  = VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
	binding.descriptorCount = kHeapDescriptorCount;
	binding.stageFlags      = VK_SHADER_STAGE_ALL;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext        = &bindingFlagsInfo;
	layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings    = &binding;
	VK_CHECK(vkCreateDescriptorSetLayout(m_vkDevice, &layoutInfo, nullptr, &m_vkHeapSetLayout));

	// The pool's list pairs with pPoolSizes[0].
	VkMutableDescriptorTypeCreateInfoEXT poolMutableInfo = {};
	poolMutableInfo.sType                          = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT;
	poolMutableInfo.mutableDescriptorTypeListCount = 1;
	poolMutableInfo.pMutableDescriptorTypeLists    = &typeList;

	const VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_MUTABLE_EXT, kHeapDescriptorCount };

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.pNext         = &poolMutableInfo;
	poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	poolInfo.maxSets       = 1;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes    = &poolSize;
	VK_CHECK(vkCreateDescriptorPool(m_vkDevice, &poolInfo, nullptr, &m_vkHeapPool));

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool     = m_vkHeapPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts        = &m_vkHeapSetLayout;
	VK_CHECK(vkAllocateDescriptorSets(m_vkDevice, &allocInfo, &m_vkHeapSet));
	SetVkObjectName("DescriptorHeap", (u64)m_vkHeapSet, VK_OBJECT_TYPE_DESCRIPTOR_SET);
}

// Immutable samplers live in the layout, so the set is never written.
void VkRenderDevice::CreateStaticSamplers()
{
	constexpr u32 kSamplerCount = static_cast< u32 >(render::eSamplerIndex::MaxIndex);

	VkSampler                    vkSamplers[kSamplerCount] = {};
	VkDescriptorSetLayoutBinding bindings[kSamplerCount]   = {};
	for (u32 i = 0; i < kSamplerCount; ++i)
	{
		StaticSamplerDesc desc = GetStaticSamplerDesc(static_cast< render::eSamplerIndex >(i));
		m_pStaticSamplers[i] = VulkanSampler::Create(*this, desc.name, std::move(desc.info));
		vkSamplers[i] = m_pStaticSamplers[i]->vkSampler();

		bindings[i].binding            = i;
		bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
		bindings[i].descriptorCount    = 1;
		bindings[i].stageFlags         = VK_SHADER_STAGE_ALL;
		bindings[i].pImmutableSamplers = &vkSamplers[i];
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = kSamplerCount;
	layoutInfo.pBindings    = bindings;
	VK_CHECK(vkCreateDescriptorSetLayout(m_vkDevice, &layoutInfo, nullptr, &m_vkSamplerSetLayout));

	const VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_SAMPLER, kSamplerCount };

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets       = 1;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes    = &poolSize;
	VK_CHECK(vkCreateDescriptorPool(m_vkDevice, &poolInfo, nullptr, &m_vkSamplerPool));

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool     = m_vkSamplerPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts        = &m_vkSamplerSetLayout;
	VK_CHECK(vkAllocateDescriptorSets(m_vkDevice, &allocInfo, &m_vkSamplerSet));

	SetVkObjectName("StaticSamplers", (u64)m_vkSamplerSet, VK_OBJECT_TYPE_DESCRIPTOR_SET);
}

// Every Slang pipeline uses this layout: the heap, the samplers, and one push range.
void VkRenderDevice::CreateGlobalPipelineLayout()
{
	const VkDescriptorSetLayout vkSetLayouts[] = { m_vkHeapSetLayout, m_vkSamplerSetLayout };

	VkPushConstantRange pushRange = {};
	pushRange.stageFlags = VK_SHADER_STAGE_ALL;
	pushRange.offset     = 0;
	pushRange.size       = render::kSlangPushBlockBytes;

	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount         = static_cast< u32 >(std::size(vkSetLayouts));
	layoutInfo.pSetLayouts            = vkSetLayouts;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges    = &pushRange;
	VK_CHECK(vkCreatePipelineLayout(m_vkDevice, &layoutInfo, nullptr, &m_vkGlobalPipelineLayout));
	SetVkObjectName("GlobalPipelineLayout", (u64)m_vkGlobalPipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT);
}

} // namespace vk