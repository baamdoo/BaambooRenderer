#include "RendererPch.h"
#include "VkBufferAllocator.h"
#include "RenderResource/VkBuffer.h"
#include "Utils/Math.hpp"

namespace vk
{

//-------------------------------------------------------------------------
// Dynamic-Buffer Allocator
//-------------------------------------------------------------------------
DynamicBufferAllocator::DynamicBufferAllocator(VkRenderDevice& rd, VkDeviceSize pageSize)
	: m_RenderDevice(rd)
	, m_MaxPageSize(pageSize)
{
	m_Alignment = m_RenderDevice.DeviceProps().limits.minUniformBufferOffsetAlignment;
}

DynamicBufferAllocator::~DynamicBufferAllocator()
{
	for (auto page : m_pPages)
		RELEASE(page);
}

DynamicBufferAllocator::Allocation DynamicBufferAllocator::Allocate(VkDeviceSize sizeInBytes)
{
	assert(sizeInBytes <= m_MaxPageSize);

	if (!m_pCurrentPage || !m_pCurrentPage->HasSpace(sizeInBytes, m_Alignment))
	{
		m_pCurrentPage = RequestPage();
	}

	return m_pCurrentPage->Allocate(sizeInBytes, m_Alignment);
}

void DynamicBufferAllocator::Reset()
{
	m_pCurrentPage = nullptr;
	m_pAvailablePages = std::deque< Page* >(m_pPages.begin(), m_pPages.end());

	for (auto page : m_pAvailablePages)
		page->Reset();
}

DynamicBufferAllocator::Page* DynamicBufferAllocator::RequestPage()
{
	Page* pPage = nullptr;
	if (!m_pAvailablePages.empty())
	{
		pPage = m_pAvailablePages.front();
		m_pAvailablePages.pop_front();
	}
	else
	{
		pPage = new Page(m_RenderDevice, m_MaxPageSize);
		m_pPages.push_back(pPage);
	}

	return pPage;
}

DynamicBufferAllocator::Page::Page(VkRenderDevice& rd, VkDeviceSize sizeInBytes)
	: m_RenderDevice(rd)
	, m_OffsetInBytes(0)
{
	m_pBuffer = VulkanUniformBuffer::Create(m_RenderDevice, "AllocatedBuffer_Dynamic", sizeInBytes, VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT);
}

DynamicBufferAllocator::Page::~Page()
{
}

DynamicBufferAllocator::Allocation DynamicBufferAllocator::Page::Allocate(VkDeviceSize sizeInBytes, VkDeviceSize alignment)
{
	VkDeviceSize alignedSize   = baamboo::math::AlignUp(sizeInBytes, alignment);
	VkDeviceSize alignedOffset = baamboo::math::AlignUp(m_OffsetInBytes, alignment);

	Allocation allocation = {};
	allocation.pBuffer       = m_pBuffer;
	allocation.offsetInBytes = alignedOffset;
	allocation.cpuHandle     = static_cast<u8*>(m_pBuffer->MappedMemory()) + alignedOffset;

	m_OffsetInBytes = alignedOffset + alignedSize;

	return allocation;
}

void DynamicBufferAllocator::Page::Reset()
{
	m_OffsetInBytes = 0;
}

bool DynamicBufferAllocator::Page::HasSpace(VkDeviceSize sizeInBytes, VkDeviceSize alignment) const
{
	VkDeviceSize alignedSize   = baamboo::math::AlignUp(sizeInBytes, alignment);
	VkDeviceSize alignedOffset = baamboo::math::AlignUp(m_OffsetInBytes, alignment);

	return alignedOffset + alignedSize <= m_pBuffer->SizeInBytes();
}

} // namespace vk
