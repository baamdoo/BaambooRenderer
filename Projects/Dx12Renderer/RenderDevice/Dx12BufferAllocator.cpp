#include "RendererPch.h"
#include "Dx12BufferAllocator.h"
#include "Dx12CommandQueue.h"
#include "Dx12CommandContext.h"
#include "Dx12ResourceManager.h"
#include "RenderResource/Dx12Buffer.h"
#include "Utils/Math.hpp"

namespace dx12
{

u64 RoundUpDivide(u64 value, u64 divisor)
{
	return (value + divisor - 1) / divisor;
}


//-------------------------------------------------------------------------
// Dynamic-Buffer Allocator
//-------------------------------------------------------------------------
DynamicBufferAllocator::DynamicBufferAllocator(Dx12RenderDevice& rd, size_t pageSize)
	: m_RenderDevice(rd)
	, m_MaxPageSize(pageSize)
{
}

DynamicBufferAllocator::~DynamicBufferAllocator()
{
	for (auto page : m_PagePool)
	{
		if (page)
		{
			delete page;
			page = nullptr;
		}
	}
}

DynamicBufferAllocator::Allocation DynamicBufferAllocator::Allocate(size_t sizeInBytes, size_t alignment)
{
	assert(sizeInBytes <= m_MaxPageSize);

	if (!m_pCurrentPage || !m_pCurrentPage->HasSpace(sizeInBytes, alignment))
	{
		m_pCurrentPage = RequestPage();
	}

	return m_pCurrentPage->Allocate(sizeInBytes, alignment);
}

DynamicBufferAllocator::Page* DynamicBufferAllocator::RequestPage()
{
	Page* pPage = nullptr;

	if (!m_AvailablePages.empty())
	{
		pPage = m_AvailablePages.front();
		m_AvailablePages.pop_front();
	}
	else
	{
		pPage = new Page(m_RenderDevice, m_MaxPageSize);
		m_PagePool.push_back(pPage);
	}

	return pPage;
}

void DynamicBufferAllocator::Reset()
{
	m_pCurrentPage = nullptr;
	m_AvailablePages = m_PagePool;

	for (auto page : m_AvailablePages)
	{
		page->Reset();
	}
}

DynamicBufferAllocator::Page::Page(Dx12RenderDevice& rd, size_t sizeInBytes)
	: m_RenderDevice(rd)
	, m_OffsetInBytes(0)
{
	m_pBuffer = Dx12Buffer::Create(m_RenderDevice, "DynamicBufferAllocator_Page", 
		{
			.count              = 1,
			.elementSizeInBytes = sizeInBytes,
			.mapDirection       = 1,
			.bufferUsage        = render::eBufferUsage_TransferSource
		});
}

DynamicBufferAllocator::Page::~Page()
{
}

bool DynamicBufferAllocator::Page::HasSpace(size_t sizeInBytes, size_t alignment) const
{
	size_t alignedSize   = baamboo::math::AlignUp(sizeInBytes, alignment);
	size_t alignedOffset = baamboo::math::AlignUp(m_OffsetInBytes, alignment);

	return alignedOffset + alignedSize <= m_pBuffer->SizeInBytes();
}

DynamicBufferAllocator::Allocation DynamicBufferAllocator::Page::Allocate(size_t sizeInBytes, size_t alignment)
{
	size_t alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);

	Allocation allocation;
	allocation.pBuffer       = m_pBuffer;
	allocation.offsetInBytes = m_OffsetInBytes;
	allocation.CPUHandle     = static_cast<u8*>(m_pBuffer->MappedMemory()) + m_OffsetInBytes;
	allocation.GPUHandle     = m_pBuffer->GetD3D12Resource()->GetGPUVirtualAddress() + m_OffsetInBytes;

	m_OffsetInBytes += alignedSize;

	return allocation;
}

void DynamicBufferAllocator::Page::Reset()
{
	m_OffsetInBytes = 0;
}


//-------------------------------------------------------------------------
// Static-Buffer Allocator
//-------------------------------------------------------------------------
StaticBufferAllocator::StaticBufferAllocator(Dx12RenderDevice& rd, const std::string& name, u64 elementSizeInBytes, u64 numElements, u32 extraUsage)
	: m_RenderDevice(rd)
	, m_Name(name)
	, m_ElementSizeInBytes(elementSizeInBytes)
	, m_ExtraUsage(extraUsage)
{
	Resize(elementSizeInBytes, static_cast<u32>(numElements));
}

StaticBufferAllocator::~StaticBufferAllocator()
{
}

StaticBufferAllocator::Allocation StaticBufferAllocator::Allocate(u32 numElements, u64 elementSizeInBytes)
{
	Allocation allocation = {};

	const u64 elementStride = elementSizeInBytes == 0 ? m_ElementSizeInBytes : elementSizeInBytes;
	BB_ASSERT(elementStride == m_ElementSizeInBytes,
	          "StaticBufferAllocator '%s' was created with stride %llu but allocation requested stride %llu",
	          m_Name.c_str(), m_ElementSizeInBytes, elementStride);

	const u64 offsetInBytes = m_OffsetInBytes;
	const u64 sizeInBytes   = numElements * elementStride;
	const u64 allocationEndBytes = offsetInBytes + sizeInBytes;
	BB_ASSERT(offsetInBytes % elementStride == 0,
	          "StaticBufferAllocator '%s' produced a non-element-aligned offset", m_Name.c_str());
	
	if (allocationEndBytes > m_SizeInBytes)
	{
		const u64 newSize = allocationEndBytes * 2;
		Resize(m_ElementSizeInBytes, static_cast<u32>(RoundUpDivide(newSize, m_ElementSizeInBytes)));
	}

	allocation.pBuffer       = m_pBuffer;
	allocation.sizeInBytes   = sizeInBytes;
	allocation.offsetInBytes = offsetInBytes;
	allocation.gpuHandle     = m_BaseGpuHandle + offsetInBytes;

	m_OffsetInBytes = allocationEndBytes;

	return allocation;
}

void StaticBufferAllocator::Reset()
{
	m_OffsetInBytes = 0;
}

void StaticBufferAllocator::Resize(u32 numElements)
{
	Resize(m_ElementSizeInBytes, numElements);
}

void StaticBufferAllocator::Resize(u64 elementSizeInBytes, u32 numElements)
{
	auto pNewBuffer = Dx12StructuredBuffer::Create(m_RenderDevice, m_Name.c_str(), elementSizeInBytes, numElements, render::eBufferUsage_TransferSource | render::eBufferUsage_TransferDest | m_ExtraUsage);

	if (m_OffsetInBytes > 0 && m_pBuffer)
	{
		auto pContext = m_RenderDevice.BeginCommand(D3D12_COMMAND_LIST_TYPE_COPY);
		pContext->CopyBuffer(pNewBuffer, m_pBuffer, m_OffsetInBytes);
		pContext->Close();
		m_RenderDevice.ExecuteCommand(std::move(pContext)).Wait();
	}

	if (m_pBuffer)
	{
		m_pBuffer.reset();
	}

	m_pBuffer       = pNewBuffer;
	m_BaseGpuHandle = m_pBuffer->GetD3D12Resource()->GetGPUVirtualAddress();
	m_SizeInBytes   = numElements * elementSizeInBytes;
}

} // namespace dx12
