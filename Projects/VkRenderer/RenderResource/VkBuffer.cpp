#include "RendererPch.h"
#include "VkBuffer.h"
#include "RenderDevice/VkCommandContext.h"

namespace vk
{

//-------------------------------------------------------------------------
// Base Buffer
//-------------------------------------------------------------------------
Buffer::Buffer(RenderDevice& device, std::string_view name, CreationInfo&& info)
	: Super(device, name, eResourceType::Buffer)
	, m_CreationInfo(std::move(info))
{
	Resize(m_CreationInfo.sizeInBytes, true);
	SetDeviceObjectName((u64)m_vkBuffer, VK_OBJECT_TYPE_BUFFER);
}

Arc< Buffer > Buffer::Create(RenderDevice& device, std::string_view name, CreationInfo&& desc)
{
	return MakeArc< Buffer >(device, name, std::move(desc));
}

Buffer::~Buffer()
{
	if (m_vmaAllocation)
		vmaDestroyBuffer(m_RenderDevice.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
}

void Buffer::Resize(u64 sizeInBytes, bool bReset)
{
	VkBuffer          vkNewBuffer   = VK_NULL_HANDLE;
	VmaAllocation     vmaAllocation = VK_NULL_HANDLE;
	VmaAllocationInfo allocationInfo;
	VkDeviceAddress   deviceAddress = 0;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size  = sizeInBytes;
	bufferInfo.usage = m_CreationInfo.usage;

	VmaAllocationCreateInfo vmaInfo = {};
	vmaInfo.flags = m_CreationInfo.bMap ?
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT : VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	vmaInfo.usage = m_CreationInfo.memoryUsage;
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
		auto& context = m_RenderDevice.BeginCommand(eCommandType::Transfer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
		context.CopyBuffer(vkNewBuffer, m_vkBuffer, SizeInBytes(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
		context.Close();
		context.Execute();

		vmaDestroyBuffer(m_RenderDevice.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
	}

	m_vkBuffer                 = vkNewBuffer;
	m_vmaAllocation            = vmaAllocation;
	m_AllocationInfo           = allocationInfo;
	m_DeviceAddress            = deviceAddress;
	m_CreationInfo.sizeInBytes = sizeInBytes;
}


//-------------------------------------------------------------------------
// Vertex Buffer : deprecated under indirect rendering system
//-------------------------------------------------------------------------
// 
//-------------------------------------------------------------------------
// Index Buffer
//-------------------------------------------------------------------------
Arc<IndexBuffer> IndexBuffer::Create(RenderDevice& device, std::string_view name, u32 numIndices, VkIndexType type)
{
	return MakeArc< IndexBuffer >(device, name, numIndices, type);
}

IndexBuffer::IndexBuffer(RenderDevice& device, std::string_view name, u32 numIndices, VkIndexType type)
	: m_IndexType(type)
	, Super(device, name,
		{
			.sizeInBytes = numIndices * GetIndexSize(),
			.bMap        = false,
			.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		})
{
}


//-------------------------------------------------------------------------
// Uniform Buffer
//-------------------------------------------------------------------------
Arc< UniformBuffer > UniformBuffer::Create(RenderDevice& device, std::string_view name, u64 sizeInBytes, VkBufferUsageFlags usage)
{
	return MakeArc< UniformBuffer >(device, name, sizeInBytes, usage);
}

UniformBuffer::UniformBuffer(RenderDevice& device, std::string_view name, u64 sizeInBytes, VkBufferUsageFlags additionalUsage)
	: Super(device, name,
		{
			.sizeInBytes = sizeInBytes,
			.bMap        = true,
			.usage       = additionalUsage | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
		})
{
}


//-------------------------------------------------------------------------
// Storage Buffer
//-------------------------------------------------------------------------
Arc<StorageBuffer> StorageBuffer::Create(RenderDevice& device, std::string_view name, u64 sizeInBytes, VkBufferUsageFlags usage)
{
	return MakeArc< StorageBuffer >(device, name, sizeInBytes, usage);
}

StorageBuffer::StorageBuffer(RenderDevice& device, std::string_view name, u64 sizeInBytes, VkBufferUsageFlags additionalUsage)
	: Super(device, name,
		{
			.sizeInBytes = sizeInBytes,
			.usage       = additionalUsage
			             | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 
		                 | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		})
{
}

} // namespace vk