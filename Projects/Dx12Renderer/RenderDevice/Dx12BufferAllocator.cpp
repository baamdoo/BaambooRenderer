#include "RendererPch.h"
#include "Dx12BufferAllocator.h"
#include "RenderResource/Dx12Buffer.h"
#include "Utils/Math.hpp"

namespace dx12
{

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

	m_OffsetInBytes += alignedSize;

	return allocation;
}

void DynamicBufferAllocator::Page::Reset()
{
	m_OffsetInBytes = 0;
}

} // namespace dx12
