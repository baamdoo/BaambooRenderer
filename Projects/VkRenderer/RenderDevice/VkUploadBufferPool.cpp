#include "RendererPch.h"
#include "VkUploadBufferPool.h"
#include "BaambooUtils/Math.hpp"

namespace vk
{

UploadBufferPool::UploadBufferPool(RenderContext& context, VkDeviceSize pageSize)
	: m_renderContext(context)
	, m_maxPageSize(pageSize)
{
	m_alignment = m_renderContext.DeviceProps().limits.minUniformBufferOffsetAlignment;
}

UploadBufferPool::~UploadBufferPool()
{
	for (auto page : m_pPages)
		RELEASE(page);
}

UploadBufferPool::Allocation UploadBufferPool::Allocate(VkDeviceSize sizeInBytes)
{
	assert(sizeInBytes <= m_maxPageSize);

	if (!m_pCurrentPage || !m_pCurrentPage->HasSpace(sizeInBytes, m_alignment))
	{
		m_pCurrentPage = RequestPage();
	}

	return m_pCurrentPage->Allocate(sizeInBytes, m_alignment);
}

void UploadBufferPool::Reset()
{
	m_pCurrentPage = nullptr;
	m_pAvailablePages = std::deque< Page* >(m_pPages.begin(), m_pPages.end());

	for (auto page : m_pAvailablePages)
		page->Reset();
}

UploadBufferPool::Page* UploadBufferPool::RequestPage()
{
	Page* pPage = nullptr;
	if (!m_pAvailablePages.empty())
	{
		pPage = m_pAvailablePages.front();
		m_pAvailablePages.pop_front();

		pPage->Activate(true);
	}
	else
	{
		pPage = new Page(m_renderContext, m_maxPageSize);
		m_pPages.push_back(pPage);
	}

	return pPage;
}

UploadBufferPool::Page::Page(RenderContext& context, VkDeviceSize sizeInBytes)
	: m_renderContext(context)
	, m_baseCpuHandle(nullptr)
	, m_offset(0)
	, m_pageSize(sizeInBytes)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = sizeInBytes;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	VmaAllocationCreateInfo vmaInfo = {};
	vmaInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	vmaInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	VmaAllocationInfo vmaAllocationInfo = {};
	VK_CHECK(vmaCreateBuffer(context.vmaAllocator(), &bufferInfo, &vmaInfo, &m_vkBuffer, &m_vmaAllocation, &vmaAllocationInfo));

	m_baseCpuHandle = vmaAllocationInfo.pMappedData;
	VK_CHECK(vmaMapMemory(context.vmaAllocator(), m_vmaAllocation, &m_baseCpuHandle));
}

UploadBufferPool::Page::~Page()
{
	vmaUnmapMemory(m_renderContext.vmaAllocator(), m_vmaAllocation);
	vmaDestroyBuffer(m_renderContext.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
}

UploadBufferPool::Allocation UploadBufferPool::Page::Allocate(VkDeviceSize sizeInBytes, VkDeviceSize alignment)
{
	VkDeviceSize alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);
	m_offset = baamboo::math::AlignUp(m_offset, alignment);

	Allocation allocation;
	allocation.vkBuffer = m_vkBuffer;
	allocation.offset = m_offset;
	allocation.size = alignedSize;
	allocation.cpuHandle = static_cast<uint8_t*>(m_baseCpuHandle) + m_offset;

	m_offset += alignedSize;

	return allocation;
}

void UploadBufferPool::Page::Reset()
{
	m_offset = 0;
	m_bActivated = false;
}

bool UploadBufferPool::Page::HasSpace(VkDeviceSize sizeInBytes, VkDeviceSize alignment) const
{
	VkDeviceSize alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);
	VkDeviceSize alignedOffset = baamboo::math::AlignUp(sizeInBytes, m_offset);

	return alignedOffset + alignedSize <= m_pageSize;
}

} // namespace vk