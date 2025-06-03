#include "RendererPch.h"
#include "VkBufferAllocator.h"
#include "VkCommandQueue.h"
#include "VkCommandContext.h"
#include "Utils/Math.hpp"

namespace vk
{


//-------------------------------------------------------------------------
// Dynamic-Buffer Allocator
//-------------------------------------------------------------------------
DynamicBufferAllocator::DynamicBufferAllocator(RenderDevice& device, VkDeviceSize pageSize)
	: m_RenderDevice(device)
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

DynamicBufferAllocator::Page::Page(RenderDevice& device, VkDeviceSize sizeInBytes)
	: m_RenderDevice(device)
	, m_BaseCpuHandle(nullptr)
	, m_Offset(0)
	, m_PageSize(sizeInBytes)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = sizeInBytes;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	VmaAllocationCreateInfo vmaInfo = {};
	vmaInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;

	VmaAllocationInfo vmaAllocationInfo = {};
	VK_CHECK(vmaCreateBuffer(m_RenderDevice.vmaAllocator(), &bufferInfo, &vmaInfo, &m_vkBuffer, &m_vmaAllocation, &vmaAllocationInfo));

	m_BaseCpuHandle = vmaAllocationInfo.pMappedData;
	//VK_CHECK(vmaMapMemory(m_RenderDevice.vmaAllocator(), m_vmaAllocation, &m_BaseCpuHandle));
}

DynamicBufferAllocator::Page::~Page()
{
	//vmaUnmapMemory(m_RenderDevice.vmaAllocator(), m_vmaAllocation);
	vmaDestroyBuffer(m_RenderDevice.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
}

DynamicBufferAllocator::Allocation DynamicBufferAllocator::Page::Allocate(VkDeviceSize sizeInBytes, VkDeviceSize alignment)
{
	VkDeviceSize alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);
	m_Offset = baamboo::math::AlignUp(m_Offset, alignment);

	Allocation allocation;
	allocation.vkBuffer = m_vkBuffer;
	allocation.offset = m_Offset;
	allocation.size = alignedSize;
	allocation.cpuHandle = static_cast<u8*>(m_BaseCpuHandle) + m_Offset;

	m_Offset += alignedSize;

	return allocation;
}

void DynamicBufferAllocator::Page::Reset()
{
	m_Offset = 0;
	m_bActivated = false;
}

bool DynamicBufferAllocator::Page::HasSpace(VkDeviceSize sizeInBytes, VkDeviceSize alignment) const
{
	VkDeviceSize alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);
	VkDeviceSize alignedOffset = baamboo::math::AlignUp(sizeInBytes, m_Offset);

	return alignedOffset + alignedSize <= m_PageSize;
}


//-------------------------------------------------------------------------
// Static-Buffer Allocator
//-------------------------------------------------------------------------
StaticBufferAllocator::StaticBufferAllocator(RenderDevice& device, VkDeviceSize bufferSize, VkBufferUsageFlags usage)
	: m_RenderDevice(device)
{
	m_Alignment = m_RenderDevice.DeviceProps().limits.minStorageBufferOffsetAlignment;
	m_UsageFlags = usage |
		           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		           VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		           VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
		           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	Resize(bufferSize);
}

StaticBufferAllocator::~StaticBufferAllocator()
{
	vmaDestroyBuffer(m_RenderDevice.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
}

StaticBufferAllocator::Allocation StaticBufferAllocator::Allocate(u32 numElements, u64 elementSizeInBytes)
{
	auto sizeInBytes = numElements * elementSizeInBytes;

	VkDeviceSize alignedSize = baamboo::math::AlignUp(sizeInBytes, m_Alignment);
	m_Offset = baamboo::math::AlignUp(m_Offset, m_Alignment);

	if (m_Offset + alignedSize > m_Size)
	{
		VkDeviceSize newSize = (m_Offset + alignedSize) * 2;
		Resize(newSize);
	}

	Allocation allocation;
	allocation.vkBuffer = m_vkBuffer;
	allocation.offset = (u32)(m_Offset / elementSizeInBytes);
	allocation.size = sizeInBytes;
	allocation.gpuHandle = m_BaseGpuHandle + m_Offset;

	m_Offset += alignedSize;

	return allocation;
}

void StaticBufferAllocator::Reset()
{
	m_Offset = 0;
}

void StaticBufferAllocator::Resize(VkDeviceSize sizeInBytes)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = sizeInBytes;
	bufferInfo.usage = m_UsageFlags;

	VmaAllocationCreateInfo vmaInfo = {};
	vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;

	VkBuffer          vkBuffer = VK_NULL_HANDLE;
	VmaAllocation     vmaAllocation = VK_NULL_HANDLE;
	VmaAllocationInfo allocationInfo = {};
	VK_CHECK(vmaCreateBuffer(m_RenderDevice.vmaAllocator(), &bufferInfo, &vmaInfo, &vkBuffer, &vmaAllocation, &allocationInfo));



	VkBufferDeviceAddressInfo addressInfo = {};
	addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = vkBuffer;
	VkDeviceAddress baseGpuHandle = vkGetBufferDeviceAddress(m_RenderDevice.vkDevice(), &addressInfo);

	if (m_Offset > 0 && m_vkBuffer != VK_NULL_HANDLE) 
	{
		auto& context = m_RenderDevice.BeginCommand(eCommandType::Transfer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
		context.CopyBuffer(vkBuffer, m_vkBuffer, m_Offset, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
		context.Close();
		context.Execute();
	}

	if (m_vkBuffer != VK_NULL_HANDLE) 
	{
		vmaDestroyBuffer(m_RenderDevice.vmaAllocator(), m_vkBuffer, m_vmaAllocation);
	}

	m_vkBuffer       = vkBuffer;
	m_vmaAllocation  = vmaAllocation;
	m_AllocationInfo = allocationInfo;
	m_BaseGpuHandle  = baseGpuHandle;
	m_Size = sizeInBytes;
}

} // namespace vk