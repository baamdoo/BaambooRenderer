#include "RendererPch.h"
#include "VkBufferAllocator.h"
#include "VkCommandQueue.h"
#include "VkCommandBuffer.h"

#include <BaambooUtils/Math.hpp>

namespace vk
{


//-------------------------------------------------------------------------
// Dynamic-Buffer Allocator
//-------------------------------------------------------------------------
DynamicBufferAllocator::DynamicBufferAllocator(RenderContext& context, VkDeviceSize pageSize)
	: m_renderContext(context)
	, m_maxPageSize(pageSize)
{
	m_alignment = m_renderContext.DeviceProps().limits.minUniformBufferOffsetAlignment;
}

DynamicBufferAllocator::~DynamicBufferAllocator()
{
	for (auto page : m_pPages)
		RELEASE(page);
}

DynamicBufferAllocator::Allocation DynamicBufferAllocator::Allocate(VkDeviceSize sizeInBytes)
{
	assert(sizeInBytes <= m_maxPageSize);

	if (!m_pCurrentPage || !m_pCurrentPage->HasSpace(sizeInBytes, m_alignment))
	{
		m_pCurrentPage = RequestPage();
	}

	return m_pCurrentPage->Allocate(sizeInBytes, m_alignment);
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
		pPage = new Page(m_renderContext, m_maxPageSize);
		m_pPages.push_back(pPage);
	}

	return pPage;
}

DynamicBufferAllocator::Page::Page(RenderContext& context, VkDeviceSize sizeInBytes)
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
	vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;

	VmaAllocationInfo vmaAllocationInfo = {};
	VK_CHECK(vmaCreateBuffer(context.vmaAllocator(), &bufferInfo, &vmaInfo, &m_vkBuffer, &m_vmaAllocation, &vmaAllocationInfo));

	m_baseCpuHandle = vmaAllocationInfo.pMappedData;
	//VK_CHECK(vmaMapMemory(context.vmaAllocator(), m_vmaAllocation, &m_baseCpuHandle));
}

DynamicBufferAllocator::Page::~Page()
{
	//vmaUnmapMemory(m_renderContext.vmaAllocator(), m_vmaAllocation);
	vmaDestroyBuffer(m_renderContext.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
}

DynamicBufferAllocator::Allocation DynamicBufferAllocator::Page::Allocate(VkDeviceSize sizeInBytes, VkDeviceSize alignment)
{
	VkDeviceSize alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);
	m_offset = baamboo::math::AlignUp(m_offset, alignment);

	Allocation allocation;
	allocation.vkBuffer = m_vkBuffer;
	allocation.offset = m_offset;
	allocation.size = alignedSize;
	allocation.cpuHandle = static_cast<u8*>(m_baseCpuHandle) + m_offset;

	m_offset += alignedSize;

	return allocation;
}

void DynamicBufferAllocator::Page::Reset()
{
	m_offset = 0;
	m_bActivated = false;
}

bool DynamicBufferAllocator::Page::HasSpace(VkDeviceSize sizeInBytes, VkDeviceSize alignment) const
{
	VkDeviceSize alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);
	VkDeviceSize alignedOffset = baamboo::math::AlignUp(sizeInBytes, m_offset);

	return alignedOffset + alignedSize <= m_pageSize;
}


//-------------------------------------------------------------------------
// Static-Buffer Allocator
//-------------------------------------------------------------------------
StaticBufferAllocator::StaticBufferAllocator(RenderContext& context, VkDeviceSize bufferSize, VkBufferUsageFlags usage)
	: m_renderContext(context)
{
	m_alignment = m_renderContext.DeviceProps().limits.minStorageBufferOffsetAlignment;
	m_usageFlags = usage |
		           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		           VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		           VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
		           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	Resize(bufferSize);
}

StaticBufferAllocator::~StaticBufferAllocator()
{
	vmaDestroyBuffer(m_renderContext.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
}

StaticBufferAllocator::Allocation StaticBufferAllocator::Allocate(u32 numElements, u64 elementSizeInBytes)
{
	auto sizeInBytes = numElements * elementSizeInBytes;

	VkDeviceSize alignedSize = baamboo::math::AlignUp(sizeInBytes, m_alignment);
	m_offset = baamboo::math::AlignUp(m_offset, m_alignment);

	if (m_offset + alignedSize > m_size)
	{
		VkDeviceSize newSize = (m_offset + alignedSize) * 2;
		Resize(newSize);
	}

	Allocation allocation;
	allocation.vkBuffer = m_vkBuffer;
	allocation.offset = (u32)(m_offset / elementSizeInBytes);
	allocation.size = sizeInBytes;
	allocation.gpuHandle = m_baseGpuHandle + m_offset;

	m_offset += alignedSize;

	return allocation;
}

void StaticBufferAllocator::Reset()
{
	m_offset = 0;
}

void StaticBufferAllocator::Resize(VkDeviceSize sizeInBytes)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = sizeInBytes;
	bufferInfo.usage = m_usageFlags;

	VmaAllocationCreateInfo vmaInfo = {};
	vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;

	VkBuffer          vkBuffer = VK_NULL_HANDLE;
	VmaAllocation     vmaAllocation = VK_NULL_HANDLE;
	VmaAllocationInfo allocationInfo = {};
	VK_CHECK(vmaCreateBuffer(m_renderContext.vmaAllocator(), &bufferInfo, &vmaInfo, &vkBuffer, &vmaAllocation, &allocationInfo));

	VkBufferDeviceAddressInfo addressInfo = {};
	addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = vkBuffer;
	VkDeviceAddress baseGpuHandle = vkGetBufferDeviceAddress(m_renderContext.vkDevice(), &addressInfo);

	if (m_offset > 0 && m_vkBuffer != VK_NULL_HANDLE) 
	{
		if (auto pTransferQueue = m_renderContext.TransferQueue())
		{
			auto& cmdBuffer = pTransferQueue->Allocate(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
			cmdBuffer.CopyBuffer(vkBuffer, m_vkBuffer, m_offset, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
			cmdBuffer.Close();
			pTransferQueue->ExecuteCommandBuffer(cmdBuffer);
		}
		else
		{
			auto& cmdBuffer = m_renderContext.GraphicsQueue().Allocate(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
			cmdBuffer.CopyBuffer(vkBuffer, m_vkBuffer, m_offset, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
			cmdBuffer.Close();
			m_renderContext.GraphicsQueue().ExecuteCommandBuffer(cmdBuffer);
		}
	}

	if (m_vkBuffer != VK_NULL_HANDLE) 
	{
		vmaDestroyBuffer(m_renderContext.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
	}

	m_vkBuffer       = vkBuffer;
	m_vmaAllocation  = vmaAllocation;
	m_allocationInfo = allocationInfo;
	m_baseGpuHandle  = baseGpuHandle;
	m_size = sizeInBytes;
}

} // namespace vk