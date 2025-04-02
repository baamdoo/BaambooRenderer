#include "RendererPch.h"
#include "Dx12UploadBufferPool.h"
#include "BaambooUtils/Math.hpp"

namespace dx12
{

UploadBufferPool::UploadBufferPool(RenderContext& context, size_t pageSize)
	: m_RenderContext(context)
	, m_MaxPageSize(pageSize)
{
}

UploadBufferPool::~UploadBufferPool()
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

UploadBufferPool::Allocation UploadBufferPool::Allocate(size_t sizeInBytes, size_t alignment)
{
	assert(sizeInBytes <= m_MaxPageSize);

	if (!m_pCurrentPage || !m_pCurrentPage->HasSpace(sizeInBytes, alignment))
	{
		m_pCurrentPage = RequestPage();
	}

	return m_pCurrentPage->Allocate(sizeInBytes, alignment);
}

UploadBufferPool::Page* UploadBufferPool::RequestPage()
{
	Page* pPage = nullptr;

	if (!m_AvailablePages.empty())
	{
		pPage = m_AvailablePages.front();
		m_AvailablePages.pop_front();
	}
	else
	{
		pPage = new Page(m_RenderContext, m_MaxPageSize);
		m_PagePool.push_back(pPage);
	}

	return pPage;
}

void UploadBufferPool::Reset()
{
	m_pCurrentPage = nullptr;
	m_AvailablePages = m_PagePool;

	for (auto page : m_AvailablePages)
	{
		page->Reset();
	}
}

UploadBufferPool::Page::Page(RenderContext& context, size_t sizeInBytes)
	: m_RenderContext(context)
	, m_BaseCPUHandle(nullptr)
	, m_BaseGPUHandle(D3D12_GPU_VIRTUAL_ADDRESS(0))
	, m_PageSize(sizeInBytes)
	, m_Offset(0)
{
	auto d3d12Device = m_RenderContext.GetD3D12Device();

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto desc = CD3DX12_RESOURCE_DESC::Buffer(m_PageSize);
	ThrowIfFailed(
		d3d12Device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE,
		&desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&m_d3d12Resource))
	);

	ThrowIfFailed(m_d3d12Resource->SetName(L"UploadBufferPage"));

	m_BaseGPUHandle = m_d3d12Resource->GetGPUVirtualAddress();
	ThrowIfFailed(m_d3d12Resource->Map(0, nullptr, &m_BaseCPUHandle));
}

UploadBufferPool::Page::~Page()
{
	if (m_d3d12Resource)
	{
		m_d3d12Resource->Unmap(0, nullptr);

		m_d3d12Resource->Release();
		m_d3d12Resource = nullptr;
	}
	m_BaseCPUHandle = nullptr;
	m_BaseGPUHandle = D3D12_GPU_VIRTUAL_ADDRESS(0);
}

bool UploadBufferPool::Page::HasSpace(size_t sizeInBytes, size_t alignment) const
{
	size_t alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);
	size_t alignedOffset = baamboo::math::AlignUp(m_Offset, alignment);

	return alignedOffset + alignedSize <= m_PageSize;
}

UploadBufferPool::Allocation UploadBufferPool::Page::Allocate(size_t sizeInBytes, size_t alignment)
{
	size_t alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);
	m_Offset = baamboo::math::AlignUp(m_Offset, alignment);

	Allocation allocation;
	allocation.CPUHandle = static_cast<uint8_t*>(m_BaseCPUHandle) + m_Offset;
	allocation.GPUHandle = m_BaseGPUHandle + m_Offset;

	m_Offset += alignedSize;

	return allocation;
}

void UploadBufferPool::Page::Reset()
{
	m_Offset = 0;
}

}