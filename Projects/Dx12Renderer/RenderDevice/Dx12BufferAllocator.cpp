#include "RendererPch.h"
#include "Dx12BufferAllocator.h"
#include "Dx12CommandQueue.h"
#include "Dx12CommandList.h"
#include "Dx12ResourceManager.h"
#include "RenderResource/Dx12Buffer.h"

#include <BaambooCore/ResourceHandle.h>
#include <BaambooUtils/Math.hpp>

namespace dx12
{


//-------------------------------------------------------------------------
// Dynamic-Buffer Allocator
//-------------------------------------------------------------------------
DynamicBufferAllocator::DynamicBufferAllocator(RenderContext& context, size_t pageSize)
	: m_RenderContext(context)
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
		pPage = new Page(m_RenderContext, m_MaxPageSize);
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

DynamicBufferAllocator::Page::Page(RenderContext& context, size_t sizeInBytes)
	: m_RenderContext(context)
	, m_BaseCpuHandle(nullptr)
	, m_BaseGpuHandle(D3D12_GPU_VIRTUAL_ADDRESS(0))
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

	m_BaseGpuHandle = m_d3d12Resource->GetGPUVirtualAddress();
	ThrowIfFailed(m_d3d12Resource->Map(0, nullptr, &m_BaseCpuHandle));
}

DynamicBufferAllocator::Page::~Page()
{
	if (m_d3d12Resource)
	{
		m_d3d12Resource->Unmap(0, nullptr);

		m_d3d12Resource->Release();
		m_d3d12Resource = nullptr;
	}
	m_BaseCpuHandle = nullptr;
	m_BaseGpuHandle = D3D12_GPU_VIRTUAL_ADDRESS(0);
}

bool DynamicBufferAllocator::Page::HasSpace(size_t sizeInBytes, size_t alignment) const
{
	size_t alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);
	size_t alignedOffset = baamboo::math::AlignUp(m_Offset, alignment);

	return alignedOffset + alignedSize <= m_PageSize;
}

DynamicBufferAllocator::Allocation DynamicBufferAllocator::Page::Allocate(size_t sizeInBytes, size_t alignment)
{
	size_t alignedSize = baamboo::math::AlignUp(sizeInBytes, alignment);
	m_Offset = baamboo::math::AlignUp(m_Offset, alignment);

	Allocation allocation;
	allocation.CPUHandle = static_cast<uint8_t*>(m_BaseCpuHandle) + m_Offset;
	allocation.GPUHandle = m_BaseGpuHandle + m_Offset;

	m_Offset += alignedSize;

	return allocation;
}

void DynamicBufferAllocator::Page::Reset()
{
	m_Offset = 0;
}


//-------------------------------------------------------------------------
// Static-Buffer Allocator
//-------------------------------------------------------------------------
StaticBufferAllocator::StaticBufferAllocator(RenderContext& context, size_t bufferSize)
	: m_RenderContext(context)
{
	Resize(bufferSize);
}

StaticBufferAllocator::~StaticBufferAllocator()
{
}

StaticBufferAllocator::Allocation StaticBufferAllocator::Allocate(u32 numElements, u64 elementSizeInBytes)
{
	auto sizeInBytes = numElements * elementSizeInBytes;

	size_t alignedSize = baamboo::math::AlignUp(sizeInBytes, m_Alignment);
	m_Offset = baamboo::math::AlignUp(m_Offset, m_Alignment);

	if (m_Offset + alignedSize > m_Size)
	{
		size_t newSize = (m_Offset + alignedSize) * 2;
		Resize(newSize);
	}

	Allocation allocation;
	allocation.buffer = m_Buffer;
	allocation.offset = (u32)(m_Offset / elementSizeInBytes);
	allocation.sizeInBytes = sizeInBytes;
	allocation.gpuHandle = m_BaseGpuHandle + m_Offset;

	m_Offset += alignedSize;

	return allocation;
}

void StaticBufferAllocator::Reset()
{
	m_Offset = 0;
}

StructuredBuffer* StaticBufferAllocator::GetBuffer() const
{
	auto& rm = m_RenderContext.GetResourceManager();
	return rm.Get(m_Buffer);
}

void StaticBufferAllocator::Resize(size_t sizeInBytes)
{
	auto& rm = m_RenderContext.GetResourceManager();

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	Buffer::CreationInfo creationInfo = {};
	creationInfo.desc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);
	creationInfo.count = 1;
	creationInfo.elementSizeInBytes = sizeInBytes;
	auto buffer = rm.Create< StructuredBuffer >(L"StaticBufferHeap", std::move(creationInfo));

	if (m_Offset > 0 && m_Buffer.IsValid())
	{
		auto& transferQueue = m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
		auto& cmdList = m_RenderContext.AllocateCommandList(D3D12_COMMAND_LIST_TYPE_COPY);
		cmdList.CopyBuffer(rm.Get(buffer), rm.Get(m_Buffer), m_Offset);
		cmdList.Close();
		transferQueue.ExecuteCommandList(&cmdList);
	}

	if (m_Buffer.IsValid())
	{
		rm.Remove(m_Buffer);
		m_Buffer.Reset();
	}

	m_Buffer = buffer;
	m_BaseGpuHandle = rm.Get(m_Buffer)->GetD3D12Resource()->GetGPUVirtualAddress();
	m_Size = sizeInBytes;
}

} // namespace dx12