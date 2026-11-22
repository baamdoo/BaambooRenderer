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
	if (IsValidIndex(m_StorageSlot))
		m_RenderDevice.FreeHeapSlot(m_StorageSlot);

	if (m_vmaAllocation)
		m_RenderDevice.DeferRelease(m_vkBuffer, m_vmaAllocation);
}

void VulkanBuffer::FlushMappedRange(u64 offsetInBytes, u64 sizeInBytes) const
{
	if (sizeInBytes == 0)
		return;

	BB_ASSERT(m_CreationInfo.mapDirection == 1, "Only host-write buffers can be flushed.");
	BB_ASSERT(m_vmaAllocation && m_AllocationInfo.pMappedData, "Cannot flush an unmapped Vulkan buffer.");
	BB_ASSERT(offsetInBytes <= SizeInBytes() && sizeInBytes <= SizeInBytes() - offsetInBytes,
	          "Mapped flush range exceeds the Vulkan buffer allocation.");
	VK_CHECK(vmaFlushAllocation(m_RenderDevice.vmaAllocator(), m_vmaAllocation, offsetInBytes, sizeInBytes));
}

void VulkanBuffer::InvalidateMappedRange(u64 offsetInBytes, u64 sizeInBytes) const
{
	if (sizeInBytes == 0)
		return;

	BB_ASSERT(m_CreationInfo.mapDirection == 2, "Only read-back buffers can be invalidated.");
	BB_ASSERT(m_vmaAllocation && m_AllocationInfo.pMappedData, "Cannot invalidate an unmapped Vulkan buffer.");
	BB_ASSERT(offsetInBytes <= SizeInBytes() && sizeInBytes <= SizeInBytes() - offsetInBytes,
	          "Mapped invalidate range exceeds the Vulkan buffer allocation.");
	VK_CHECK(vmaInvalidateAllocation(m_RenderDevice.vmaAllocator(), m_vmaAllocation, offsetInBytes, sizeInBytes));
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
	switch (m_CreationInfo.mapDirection)
	{
	case 0:
		vmaInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		break;
	case 1:
		vmaInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		break;
	case 2:
		vmaInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		vmaInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		vmaInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		break;
	default:
		BB_ASSERT(false, "Unsupported Vulkan buffer map direction: %u", m_CreationInfo.mapDirection);
		break;
	}
	vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
	VK_CHECK(vmaCreateBuffer(m_RenderDevice.vmaAllocator(), &bufferInfo, &vmaInfo, &vkNewBuffer, &vmaAllocation, &allocationInfo));

	if (bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	{
		VkBufferDeviceAddressInfo addressInfo = {};
		addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		addressInfo.buffer = vkNewBuffer;
		deviceAddress = vkGetBufferDeviceAddress(m_RenderDevice.vkDevice(), &addressInfo);
		assert(deviceAddress);
	}

	if (m_vkBuffer != VK_NULL_HANDLE)
	{
		if (!bReset)
		{
			const VkDeviceSize copySize = std::min< VkDeviceSize >(SizeInBytes(), sizeInBytes);
			if (copySize > 0)
			{
				if (m_CreationInfo.mapDirection == 1)
					FlushMappedRange(0, copySize);

				auto pContext = m_RenderDevice.BeginCommand(eCommandType::Transfer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
				pContext->CopyBuffer(vkNewBuffer, m_vkBuffer, copySize);
				pContext->Close();
				m_RenderDevice.ExecuteCommand(pContext); // synchronous — GPU caught up to transfer queue
			}
		}
		m_RenderDevice.DeferRelease(m_vkBuffer, m_vmaAllocation);
	}

	m_vkBuffer       = vkNewBuffer;
	m_vmaAllocation  = vmaAllocation;
	m_AllocationInfo = allocationInfo;
	m_DeviceAddress  = deviceAddress;

	if (bufferInfo.usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
	{
		// a new slot each time: frames in flight still read the old buffer through the old one
		if (IsValidIndex(m_StorageSlot))
			m_RenderDevice.FreeHeapSlot(m_StorageSlot);

		m_StorageSlot = m_RenderDevice.AllocateHeapSlot();
		m_RenderDevice.WriteHeapBuffer(m_StorageSlot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_vkBuffer);
	}

	m_CreationInfo.count              = 1;
	m_CreationInfo.elementSizeInBytes = sizeInBytes;
}


//-------------------------------------------------------------------------
// Uniform Buffer
//-------------------------------------------------------------------------
Arc< VulkanUniformBuffer > VulkanUniformBuffer::Create(VkRenderDevice& rd, const char* name, u64 sizeInBytes, VkBufferUsageFlags2 usage)
{
	return MakeArc< VulkanUniformBuffer >(rd, name, sizeInBytes, usage);
}

VulkanUniformBuffer::VulkanUniformBuffer(VkRenderDevice& rd, const char* name, u64 sizeInBytes, VkBufferUsageFlags2 additionalUsage)
	: Super(rd, name,
		{
			.count              = 1,
			.elementSizeInBytes = sizeInBytes,
			.mapDirection       = 1,
			.bufferUsage        = additionalUsage | VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
		})
{
}

} // namespace vk
