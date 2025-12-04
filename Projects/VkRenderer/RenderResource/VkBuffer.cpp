#include "RendererPch.h"
#include "VkBuffer.h"
#include "RenderDevice/VkCommandContext.h"

namespace vk
{

//-------------------------------------------------------------------------
// Base Buffer
//-------------------------------------------------------------------------
Arc< VulkanBuffer > VulkanBuffer::Create(VkRenderDevice& rd, const char* name, CreationInfo&& desc)
{
	return MakeArc< VulkanBuffer >(rd, name, std::move(desc));
}

Arc< VulkanBuffer > VulkanBuffer::CreateEmpty(VkRenderDevice& rd, const char* name)
{
	return MakeArc< VulkanBuffer >(rd, name);
}

VulkanBuffer::VulkanBuffer(VkRenderDevice& rd, const char* name)
	: render::Buffer(name)
	, VulkanResource(rd, name)
{
}

VulkanBuffer::VulkanBuffer(VkRenderDevice& rd, const char* name, CreationInfo&& info)
	: render::Buffer(name, std::move(info))
	, VulkanResource(rd, name)
{
	Resize(m_CreationInfo.count * m_CreationInfo.elementSizeInBytes, true);
	SetDeviceObjectName((u64)m_vkBuffer, VK_OBJECT_TYPE_BUFFER);
}

VulkanBuffer::~VulkanBuffer()
{
	if (m_vmaAllocation)
		vmaDestroyBuffer(m_RenderDevice.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
}

void VulkanBuffer::Resize(u64 sizeInBytes, bool bReset)
{
	VkBuffer          vkNewBuffer   = VK_NULL_HANDLE;
	VmaAllocation     vmaAllocation = VK_NULL_HANDLE;
	VmaAllocationInfo allocationInfo;
	VkDeviceAddress   deviceAddress = 0;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size  = sizeInBytes;
	bufferInfo.usage = VK_BUFFER_USAGE_FLAGS(m_CreationInfo.bufferUsage);

	VmaAllocationCreateInfo vmaInfo = {};
	vmaInfo.flags = m_CreationInfo.bMap ?
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT : VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
	VK_CHECK(vmaCreateBuffer(m_RenderDevice.vmaAllocator(), &bufferInfo, &vmaInfo, &vkNewBuffer, &vmaAllocation, &allocationInfo));

	if (bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	{
		VkBufferDeviceAddressInfo addressInfo = {};
		addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		addressInfo.buffer = vkNewBuffer;
		deviceAddress = vkGetBufferDeviceAddress(m_RenderDevice.vkDevice(), &addressInfo);
		assert(deviceAddress);
	}

	if (!bReset && m_vkBuffer != VK_NULL_HANDLE)
	{
		auto pContext = m_RenderDevice.BeginCommand(eCommandType::Transfer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
		pContext->CopyBuffer(vkNewBuffer, m_vkBuffer, SizeInBytes(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
		pContext->Close();
		m_RenderDevice.ExecuteCommand(pContext);

		vmaDestroyBuffer(m_RenderDevice.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
	}

	m_vkBuffer                 = vkNewBuffer;
	m_vmaAllocation            = vmaAllocation;
	m_AllocationInfo           = allocationInfo;
	m_DeviceAddress            = deviceAddress;

	m_CreationInfo.count              = 1;
	m_CreationInfo.elementSizeInBytes = sizeInBytes;
}


//-------------------------------------------------------------------------
// Vertex Buffer : deprecated under indirect rendering system
//-------------------------------------------------------------------------
// 
//-------------------------------------------------------------------------
// Index Buffer
//-------------------------------------------------------------------------
Arc< VulkanIndexBuffer > VulkanIndexBuffer::Create(VkRenderDevice& rd, const char* name, u32 numIndices, VkIndexType type)
{
	return MakeArc< VulkanIndexBuffer >(rd, name, numIndices, type);
}

VulkanIndexBuffer::VulkanIndexBuffer(VkRenderDevice& rd, const char* name, u32 numIndices, VkIndexType type)
	: m_IndexType(type)
	, Super(rd, name,
		{
			.count              = numIndices,
			.elementSizeInBytes = GetIndexSize(),
			.bMap               = false,
			.bufferUsage        = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		})
{
}


//-------------------------------------------------------------------------
// Uniform Buffer
//-------------------------------------------------------------------------
Arc< VulkanUniformBuffer > VulkanUniformBuffer::Create(VkRenderDevice& rd, const char* name, u64 sizeInBytes, VkBufferUsageFlags usage)
{
	return MakeArc< VulkanUniformBuffer >(rd, name, sizeInBytes, usage);
}

VulkanUniformBuffer::VulkanUniformBuffer(VkRenderDevice& rd, const char* name, u64 sizeInBytes, VkBufferUsageFlags additionalUsage)
	: Super(rd, name,
		{
			.count              = 1,
			.elementSizeInBytes = sizeInBytes,
			.bMap               = true,
			.bufferUsage        = additionalUsage | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
		})
{
}


//-------------------------------------------------------------------------
// Storage Buffer
//-------------------------------------------------------------------------
Arc< VulkanStorageBuffer > VulkanStorageBuffer::Create(VkRenderDevice& rd, const char* name, u64 sizeInBytes, VkBufferUsageFlags usage)
{
	return MakeArc< VulkanStorageBuffer >(rd, name, sizeInBytes, usage);
}

VulkanStorageBuffer::VulkanStorageBuffer(VkRenderDevice& rd, const char* name, u64 sizeInBytes, VkBufferUsageFlags additionalUsage)
	: Super(rd, name,
		{
			.count              = 1,
			.elementSizeInBytes = sizeInBytes,
			.bufferUsage        = additionalUsage
			                    | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 
		                        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		})
{
}

} // namespace vk