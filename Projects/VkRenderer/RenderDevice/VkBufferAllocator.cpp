#include "RendererPch.h"
#include "VkBufferAllocator.h"
#include "VkCommandQueue.h"
#include "VkCommandContext.h"
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

		pPage->Activate(true);
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
	allocation.sizeInBytes   = alignedSize;
	allocation.offsetInBytes = alignedOffset;
	allocation.cpuHandle     = static_cast<u8*>(m_pBuffer->MappedMemory()) + alignedOffset;

	m_OffsetInBytes = alignedOffset + alignedSize;

	return allocation;
}

void DynamicBufferAllocator::Page::Reset()
{
	m_OffsetInBytes = 0;
	m_bActivated    = false;
}

bool DynamicBufferAllocator::Page::HasSpace(VkDeviceSize sizeInBytes, VkDeviceSize alignment) const
{
	VkDeviceSize alignedSize   = baamboo::math::AlignUp(sizeInBytes, alignment);
	VkDeviceSize alignedOffset = baamboo::math::AlignUp(m_OffsetInBytes, alignment);

	return alignedOffset + alignedSize <= m_pBuffer->SizeInBytes();
}


//-------------------------------------------------------------------------
// Static-Buffer Allocator
//-------------------------------------------------------------------------
StaticBufferAllocator::StaticBufferAllocator(VkRenderDevice& rd, VkDeviceSize bufferSize, VkBufferUsageFlags2 usage)
	: m_RenderDevice(rd)
{
	m_Alignment  = m_RenderDevice.DeviceProps().limits.minStorageBufferOffsetAlignment;
	m_UsageFlags = usage |
		           VK_BUFFER_USAGE_2_TRANSFER_DST_BIT |
		           VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT |
		           VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;

	Resize(std::min(bufferSize, m_RenderDevice.DeviceMaintenance3Props().maxMemoryAllocationSize));
}

StaticBufferAllocator::~StaticBufferAllocator()
{
}

StaticBufferAllocator::Allocation StaticBufferAllocator::Allocate(u32 numElements, u64 elementSizeInBytes)
{
	auto sizeInBytes = numElements * elementSizeInBytes;

	if (m_Offset + sizeInBytes > m_pAllocatedBuffer->SizeInBytes())
	{
		VkDeviceSize newSize = (m_Offset + sizeInBytes) * 2;
		Resize(newSize);
	}

	Allocation allocation = {};
	allocation.pBuffer     = m_pAllocatedBuffer;
	allocation.offset      = (u32)(m_Offset / elementSizeInBytes);
	allocation.sizeInBytes = sizeInBytes;
	allocation.gpuHandle   = m_pAllocatedBuffer->DeviceAddress() + m_Offset;

	m_Offset += sizeInBytes;

	return allocation;
}

void StaticBufferAllocator::Reset()
{
	m_Offset = 0;
}

VkDescriptorBufferInfo StaticBufferAllocator::GetDescriptorInfo(u64 offset) const
{
	VkDescriptorBufferInfo descriptorInfo = {};
	descriptorInfo.buffer = m_pAllocatedBuffer->vkBuffer();
	descriptorInfo.offset = offset;
	descriptorInfo.range  = GetAllocatedSize();
	return descriptorInfo;
}

void StaticBufferAllocator::Resize(VkDeviceSize sizeInBytes)
{
	if (m_pAllocatedBuffer == nullptr)
	{
		if (m_UsageFlags & VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT)
			m_pAllocatedBuffer = VulkanStorageBuffer::Create(m_RenderDevice, "AllocatedBuffer_Static", sizeInBytes, m_UsageFlags);
		else if (m_UsageFlags & VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT)
			m_pAllocatedBuffer = VulkanUniformBuffer::Create(m_RenderDevice, "AllocatedBuffer_Static", sizeInBytes, m_UsageFlags);
		else if (m_UsageFlags & VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT)
			m_pAllocatedBuffer = VulkanIndexBuffer::Create(m_RenderDevice, "AllocatedBuffer_Static", (u32)(sizeInBytes / sizeof(u32)), VK_INDEX_TYPE_UINT32);
		return;
	}

	m_pAllocatedBuffer->Resize(sizeInBytes);
}

} // namespace vk
