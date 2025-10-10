#include "RendererPch.h"
#include "Dx12CommandContext.h"
#include "Dx12DescriptorHeap.h"
#include "Dx12DescriptorPool.h"

namespace dx12
{

DescriptorHeap::DescriptorHeap(RenderDevice& device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 maxDescriptors)
    : m_RenderDevice(device)
    , m_NumDescriptors(maxDescriptors)
    , m_Type(type)
{
    m_pDescriptorPool =
        new DescriptorPool(device, type, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, m_NumDescriptors);
}

DescriptorHeap::~DescriptorHeap()
{
    Reset();

	RELEASE(m_pDescriptorPool);
}

void DescriptorHeap::Reset()
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

void DescriptorHeap::ParseRootSignature(RootSignature* pRootsignature)
{
	assert(pRootsignature);
    m_pCurrentRS = pRootsignature;

    m_DescriptorTableBitMask = pRootsignature->GetDescriptorTableBitMask(m_Type);
    m_CachedDescriptorAllocations.emplace(m_pCurrentRS, std::array< DescriptorAllocation, MAX_ROOT_INDEX >{});
}

u32 DescriptorHeap::StageDescriptors(u32 rootIndex, u32 numDescriptors, u32 offset, D3D12_CPU_DESCRIPTOR_HANDLE srcHandle)
{
    assert(m_pCurrentRS);
    assert(rootIndex < MAX_ROOT_INDEX && numDescriptors < m_NumDescriptors && offset + numDescriptors < m_NumDescriptors);

    auto d3d12Device = m_RenderDevice.GetD3D12Device();
    m_CachedDescriptorAllocations[m_pCurrentRS][rootIndex] = m_pDescriptorPool->Allocate(numDescriptors);

    auto& allocation = m_CachedDescriptorAllocations[m_pCurrentRS][rootIndex];
    auto  dstHandle  = allocation.GetCPUHandle(offset);
    d3d12Device->CopyDescriptorsSimple(numDescriptors, dstHandle, srcHandle, m_Type);

    m_DescriptorTableDirtyFlags |= (1LL << rootIndex);

    return allocation.Index(offset);
}

u32 DescriptorHeap::StageDescriptors(u32 rootIndex, u32 offset, std::vector< D3D12_CPU_DESCRIPTOR_HANDLE >&& srcHandles)
{
    u32 numDescriptors = static_cast<u32>(srcHandles.size());
    assert(rootIndex < MAX_ROOT_INDEX && numDescriptors < m_NumDescriptors && offset + numDescriptors < m_NumDescriptors);

    auto d3d12Device = m_RenderDevice.GetD3D12Device();
    m_CachedDescriptorAllocations[m_pCurrentRS][rootIndex] = m_pDescriptorPool->Allocate(numDescriptors);

    u32   offset_    = offset;
    auto& allocation = m_CachedDescriptorAllocations[m_pCurrentRS][rootIndex];
    for (const auto& handle : srcHandles)
    {
        auto dstHandle = allocation.GetCPUHandle(offset_++);
        d3d12Device->CopyDescriptorsSimple(1, dstHandle, handle, m_Type);
    }

    m_DescriptorTableDirtyFlags |= (1LL << rootIndex);
    
    return allocation.Index(offset);
}

void DescriptorHeap::CommitDescriptorsForDraw(CommandContext& context)
{
    CommitDescriptorTables(context, &ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable);
}

void DescriptorHeap::CommitDescriptorsForDispatch(CommandContext& context)
{
    CommitDescriptorTables(context, &ID3D12GraphicsCommandList::SetComputeRootDescriptorTable);
}

void DescriptorHeap::CommitDescriptorTables(CommandContext& context, std::function< void(ID3D12GraphicsCommandList*, u32, D3D12_GPU_DESCRIPTOR_HANDLE) > setFunc)
{
    if (m_DescriptorTableDirtyFlags)
    {
        auto d3d12CommandList = context.GetD3D12CommandList();

        DWORD rootIndex;
        while (_BitScanForward64(&rootIndex, m_DescriptorTableDirtyFlags))
        {
            const auto& allocation = m_CachedDescriptorAllocations[m_pCurrentRS][rootIndex];
            setFunc(d3d12CommandList, rootIndex, allocation.GetGPUHandle());

            m_DescriptorTableDirtyFlags ^= (1LL << rootIndex);
        }
    }
}

ID3D12DescriptorHeap* DescriptorHeap::GetD3D12DescriptorHeap() const
{
	return m_pDescriptorPool->GetD3D12DescriptorHeap();
}

}