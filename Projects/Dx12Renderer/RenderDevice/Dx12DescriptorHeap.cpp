#include "RendererPch.h"
#include "Dx12CommandList.h"
#include "Dx12DescriptorHeap.h"
#include "Dx12DescriptorPool.h"

namespace dx12
{

DescriptorHeap::DescriptorHeap(RenderContext& context, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 maxDescriptors)
    : m_RenderContext(context)
    , m_NumDescriptors(maxDescriptors)
    , m_Type(type)
{
    m_pDescriptorPool =
        new DescriptorPool(context, type, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, m_NumDescriptors);
}

DescriptorHeap::~DescriptorHeap()
{
    Reset();

	RELEASE(m_pDescriptorPool);
}

void DescriptorHeap::Reset()
{
    m_DescriptorTableBitMask = 0;
    m_DescriptorTableDirtyFlags = 0;

    for (auto& allocation : m_CachedDescriptorAllocations)
    {
        allocation.Free();
    }
}

void DescriptorHeap::ParseRootSignature(const RootSignature* pRootsignature)
{
	assert(pRootsignature);

    u32 numParameters = pRootsignature->GetNumParameters();

    m_DescriptorTableBitMask = pRootsignature->GetDescriptorTableBitMask(m_Type);
    u64 descriptorTableBitMask = m_DescriptorTableBitMask;

    DWORD rootIndex;
    while (_BitScanForward64(&rootIndex, descriptorTableBitMask) && rootIndex < numParameters)
    {
        u32 numDescriptors = pRootsignature->GetNumDescriptors(rootIndex);
        m_CachedDescriptorAllocations[rootIndex] = m_pDescriptorPool->Allocate(numDescriptors);

        descriptorTableBitMask ^= (1LL << rootIndex);
    }
}

void DescriptorHeap::StageDescriptors(u32 rootIndex, u32 numDescriptors, u32 offset, D3D12_CPU_DESCRIPTOR_HANDLE srcHandle)
{
    assert(rootIndex < MAX_ROOT_INDEX && numDescriptors < m_NumDescriptors && offset + numDescriptors < m_NumDescriptors);

    auto d3d12Device = m_RenderContext.GetD3D12Device();
    auto& allocation = m_CachedDescriptorAllocations[rootIndex];

    auto dstHandle = allocation.GetCPUHandle(offset);
    d3d12Device->CopyDescriptorsSimple(numDescriptors, dstHandle, srcHandle, m_Type);

    m_DescriptorTableDirtyFlags |= (1LL << rootIndex);
}

void DescriptorHeap::CommitDescriptorsForDraw(CommandList& commandList)
{
    CommitDescriptorTables(commandList, &ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable);
}

void DescriptorHeap::CommitDescriptorsForDispatch(CommandList& commandList)
{
    CommitDescriptorTables(commandList, &ID3D12GraphicsCommandList::SetComputeRootDescriptorTable);
}

void DescriptorHeap::CommitDescriptorTables(CommandList& commandList, std::function< void(ID3D12GraphicsCommandList*, u32, D3D12_GPU_DESCRIPTOR_HANDLE) > setFunc)
{
    if (m_DescriptorTableDirtyFlags)
    {
        auto d3d12CommandList = commandList.GetD3D12CommandList();

        DWORD rootIndex;
        while (_BitScanForward64(&rootIndex, m_DescriptorTableDirtyFlags))
        {
            const auto& allocation = m_CachedDescriptorAllocations[rootIndex];
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