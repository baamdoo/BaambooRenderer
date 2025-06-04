#include "RendererPch.h"
#include "Dx12BufferAllocator.h"
#include "Dx12CommandQueue.h"
#include "Dx12CommandContext.h"
#include "Dx12ResourceManager.h"
#include "RenderResource/Dx12Buffer.h"
#include "Utils/Math.hpp"

namespace dx12
{


//-------------------------------------------------------------------------
// Dynamic-Buffer Allocator
//-------------------------------------------------------------------------
DynamicBufferAllocator::DynamicBufferAllocator(RenderDevice& device, size_t pageSize)
	: m_RenderDevice(device)
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

DynamicBufferAllocator::Page::Page(RenderDevice& device, size_t sizeInBytes)
	: m_RenderDevice(device)
	, m_BaseCpuHandle(nullptr)
	, m_BaseGpuHandle(D3D12_GPU_VIRTUAL_ADDRESS(0))
	, m_PageSize(sizeInBytes)
	, m_Offset(0)
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

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
StaticBufferAllocator::StaticBufferAllocator(RenderDevice& device, const std::wstring& name, size_t bufferSize)
	: m_RenderDevice(device)
	, m_Name(name)
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
	allocation.pBuffer = m_pBuffer;
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

void StaticBufferAllocator::Resize(size_t sizeInBytes)
{
	auto pNewBuffer = MakeArc< StructuredBuffer >(
		m_RenderDevice, 
		m_Name, 
		Buffer::CreationInfo
		{
			ResourceCreationInfo
			{
				CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes)
			},
			1,
			sizeInBytes
		});

	if (m_Offset > 0 && m_pBuffer)
	{
		auto& commandQueue = m_RenderDevice.CopyQueue();
		auto& context = commandQueue.Allocate();
		context.CopyBuffer(pNewBuffer, m_pBuffer, m_Offset);
		context.Close();
		commandQueue.ExecuteCommandList(&context);
	}

	if (m_pBuffer)
	{
		m_pBuffer.reset();
	}

	m_pBuffer = pNewBuffer;
	m_BaseGpuHandle = m_pBuffer->GetD3D12Resource()->GetGPUVirtualAddress();
	m_Size = sizeInBytes;
}

} // namespace dx12