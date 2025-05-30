#include "RendererPch.h"
#include "VkBuffer.h"

namespace vk
{

Buffer::Buffer(RenderContext& context, std::wstring_view name)
	: Super(context, name)
{
}

Buffer::Buffer(RenderContext& context, std::wstring_view name, CreationInfo&& info)
	: Super(context, name)
	, m_Count(info.count)
	, m_ElementSizeInBytes(info.elementSize)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = SizeInBytes();
	bufferInfo.usage = info.bufferUsage;

	VmaAllocationCreateInfo vmaInfo = {};
	vmaInfo.flags = info.bMap ? 
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT : VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	vmaInfo.usage = info.memoryUsage;

	VK_CHECK(vmaCreateBuffer(context.vmaAllocator(), &bufferInfo, &vmaInfo, &m_vkBuffer, &m_vmaAllocation, &m_vmaAllocationInfo));
}

Buffer::~Buffer()
{
	vmaDestroyBuffer(m_RenderContext.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
}


VertexBuffer::VertexBuffer(RenderContext& context, std::wstring_view name, CreationInfo&& info)
	: Buffer(context, name, std::move(info)) {}
IndexBuffer::IndexBuffer(RenderContext& context, std::wstring_view name, CreationInfo&& info)
	: Buffer(context, name, std::move(info)) {}

} // namespace vk