#include "RendererPch.h"
#include "Dx12CommandContext.h"
#include "Dx12DescriptorHeap.h"
#include "Dx12DescriptorPool.h"

namespace dx12
{

Dx12DescriptorHeap::Dx12DescriptorHeap(Dx12RenderDevice& rd, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 maxDescriptors)
    : m_RenderDevice(rd)
    , m_NumDescriptors(maxDescriptors)
    , m_Type(type)
{
    m_pDescriptorPool =
        new DescriptorPool(m_RenderDevice, type, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, m_NumDescriptors);
}

Dx12DescriptorHeap::~Dx12DescriptorHeap()
{
    Reset();

	RELEASE(m_pDescriptorPool);
}

void Dx12DescriptorHeap::Reset()
{
    m_pCurrentRS = nullptr;

    m_DescriptorTableBitMask    = 0;
    m_DescriptorTableDirtyFlags = 0;

    for (auto& pair : m_CachedDescriptorAllocations)
    {
        for (auto& allocation : pair.second)
			allocation.Free();
    }
}

void Dx12DescriptorHeap::ParseRootSignature(Arc< Dx12RootSignature > pRootSignature)
{
	assert(pRootSignature);
    m_pCurrentRS = pRootSignature;

    m_DescriptorTableBitMask = m_pCurrentRS->GetDescriptorTableBitMask(m_Type);
    m_CachedDescriptorAllocations.emplace(m_pCurrentRS.get(), std::array< DescriptorAllocation, MAX_ROOT_INDEX >{});

    auto mask = m_DescriptorTableBitMask;
    DWORD rootIndex;
    while (_BitScanForward64(&rootIndex, mask))
    {
        auto numDescriptors = m_pCurrentRS->GetNumDescriptors(rootIndex);
        // Allocate bindless descriptors on-demand
        if (numDescriptors < UINT_MAX)
        {
            m_CachedDescriptorAllocations[m_pCurrentRS.get()][rootIndex] = m_pDescriptorPool->Allocate(numDescriptors);
        }

        mask ^= (1LL << rootIndex);
    }
}

void Dx12DescriptorHeap::ParseGlobalRootSignature(Arc< Dx12RootSignature > pRootSignature)
{
    assert(pRootSignature);
    m_pCurrentRS = pRootSignature;

    m_DescriptorTableBitMask = m_pCurrentRS->GetDescriptorTableBitMask(m_Type);
    m_CachedDescriptorAllocations.emplace(m_pCurrentRS.get(), std::array< DescriptorAllocation, MAX_ROOT_INDEX >{});

    auto mask = m_DescriptorTableBitMask;
    DWORD rootIndex;
    while (_BitScanForward64(&rootIndex, mask))
    {
        auto numDescriptors = m_pCurrentRS->GetNumDescriptors(rootIndex);
        // Allocate bindless descriptors on-demand
        if (numDescriptors < UINT_MAX)
        {
            m_CachedDescriptorAllocations[m_pCurrentRS.get()][rootIndex] = m_pDescriptorPool->Allocate(numDescriptors);
        }

        mask ^= (1LL << rootIndex);
    }
}

u32 Dx12DescriptorHeap::StageDescriptor(u32 rootIndex, u32 numDescriptors, u32 offset, D3D12_CPU_DESCRIPTOR_HANDLE srcHandle)
{
    assert(m_pCurrentRS);
    assert(rootIndex < MAX_ROOT_INDEX && numDescriptors < m_NumDescriptors && offset + numDescriptors < m_NumDescriptors);

    auto d3d12Device = m_RenderDevice.GetD3D12Device();
    // On-demand bindless descriptors allocation
    if (m_pCurrentRS->GetNumDescriptors(rootIndex) == UINT_MAX)
    {
        assert(!m_CachedDescriptorAllocations[m_pCurrentRS.get()][rootIndex].IsValid() && "Do not mix bindless descriptor and others in a same root index");
        m_CachedDescriptorAllocations[m_pCurrentRS.get()][rootIndex] = m_pDescriptorPool->Allocate(numDescriptors);
    }

    auto& allocation = m_CachedDescriptorAllocations[m_pCurrentRS.get()][rootIndex];
    auto  dstHandle  = allocation.GetCPUHandle(offset);
    d3d12Device->CopyDescriptorsSimple(numDescriptors, dstHandle, srcHandle, m_Type);

    m_DescriptorTableDirtyFlags |= (1LL << rootIndex);

    return allocation.Index(offset);
}

u32 Dx12DescriptorHeap::StageDescriptors(u32 rootIndex, u32 offset, std::vector< D3D12_CPU_DESCRIPTOR_HANDLE >&& srcHandles)
{
    u32 numDescriptors = static_cast<u32>(srcHandles.size());
    assert(rootIndex < MAX_ROOT_INDEX && numDescriptors < m_NumDescriptors && offset + numDescriptors < m_NumDescriptors);

    auto d3d12Device = m_RenderDevice.GetD3D12Device();
    // On-demand bindless descriptors allocation
    if (m_pCurrentRS->GetNumDescriptors(rootIndex) == UINT_MAX)
    {
        assert(!m_CachedDescriptorAllocations[m_pCurrentRS.get()][rootIndex].IsValid());
        m_CachedDescriptorAllocations[m_pCurrentRS.get()][rootIndex] = m_pDescriptorPool->Allocate(numDescriptors);
    }

    u32   offset_    = offset;
    auto& allocation = m_CachedDescriptorAllocations[m_pCurrentRS.get()][rootIndex];
    for (const auto& handle : srcHandles)
    {
        auto dstHandle = allocation.GetCPUHandle(offset_++);
        d3d12Device->CopyDescriptorsSimple(1, dstHandle, handle, m_Type);
    }

    m_DescriptorTableDirtyFlags |= (1LL << rootIndex);
    
    return allocation.Index(offset);
}

void Dx12DescriptorHeap::CommitDescriptorsForDraw(ID3D12GraphicsCommandList2* d3d12CommandList2)
{
    if (m_DescriptorTableDirtyFlags)
    {
        DWORD rootIndex;
        while (_BitScanForward64(&rootIndex, m_DescriptorTableDirtyFlags))
        {
            const auto& allocation = m_CachedDescriptorAllocations[m_pCurrentRS.get()][rootIndex];
            d3d12CommandList2->SetGraphicsRootDescriptorTable(rootIndex, allocation.GetGPUHandle());

            m_DescriptorTableDirtyFlags ^= (1LL << rootIndex);
        }
    }
}

void Dx12DescriptorHeap::CommitDescriptorsForDispatch(ID3D12GraphicsCommandList2* d3d12CommandList2)
{
    if (m_DescriptorTableDirtyFlags)
    {
        DWORD rootIndex;
        while (_BitScanForward64(&rootIndex, m_DescriptorTableDirtyFlags))
        {
            const auto& allocation = m_CachedDescriptorAllocations[m_pCurrentRS.get()][rootIndex];
            d3d12CommandList2->SetComputeRootDescriptorTable(rootIndex, allocation.GetGPUHandle());

            m_DescriptorTableDirtyFlags ^= (1LL << rootIndex);
        }
    }
}

ID3D12DescriptorHeap* Dx12DescriptorHeap::GetD3D12DescriptorHeap() const
{
	return m_pDescriptorPool->GetD3D12DescriptorHeap();
}

}