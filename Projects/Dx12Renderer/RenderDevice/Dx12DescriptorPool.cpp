#include "RendererPch.h"
#include "Dx12DescriptorPool.h"

namespace dx12
{

DescriptorPool::DescriptorPool(Dx12RenderDevice& rd, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags, u32 maxDescriptorCount)
	: m_RenderDevice(rd)
	, m_d3d12HeapType(type)
	, m_NumDescriptors(maxDescriptorCount)
	, m_DescriptorSize(0)
{
    auto d3d12Device = m_RenderDevice.GetD3D12Device();

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = m_NumDescriptors;
    heapDesc.Type = type;
    heapDesc.Flags = flags;

    ThrowIfFailed(d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_d3d12DescriptorHeap)));

#ifdef _DEBUG
    if (heapDesc.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        static u32 index = 0;
        std::wstring name = L"ShaderVisibleDescriptorHeap_" + std::to_wstring(index);
        ThrowIfFailed(m_d3d12DescriptorHeap->SetName(name.c_str()));
    }
    else
    {
        ThrowIfFailed(m_d3d12DescriptorHeap->SetName(L"CpuCachedDescriptorHeap"));
    }
#endif

    m_DescriptorSize = d3d12Device->GetDescriptorHandleIncrementSize(type);
    m_BaseCPUHandle = m_d3d12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    m_BaseGPUHandle = heapDesc.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE ? 
        m_d3d12DescriptorHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE();

    m_AvailableDescriptors.push_back({ 0, m_NumDescriptors });
}

DescriptorPool::~DescriptorPool()
{
    COM_RELEASE(m_d3d12DescriptorHeap);
}

DescriptorAllocation DescriptorPool::Allocate(u32 numDescriptors)
{
    // std::lock_guard< std::mutex > lock(m_Mutex);

    for (auto it = m_AvailableDescriptors.begin(); it != m_AvailableDescriptors.end(); ++it) 
    {
        if (it->second >= numDescriptors) 
        {
            u32 offset = it->first;
            it->first += numDescriptors;
            it->second -= numDescriptors;

            if (it->second == 0)
                m_AvailableDescriptors.erase(it);

            return DescriptorAllocation(
                this,
                CD3DX12_CPU_DESCRIPTOR_HANDLE(m_BaseCPUHandle, offset, m_DescriptorSize),
                CD3DX12_GPU_DESCRIPTOR_HANDLE(m_BaseGPUHandle, offset, m_DescriptorSize),
                numDescriptors,
                offset
            );
        }
    }

    assert("Failed to allocate descriptor");
    return DescriptorAllocation();
}

void DescriptorPool::Free(DescriptorAllocation& allocation)
{
    // std::lock_guard<std::mutex> lock(m_Mutex);

    u32 offset = static_cast<u32>((allocation.GetCPUHandle().ptr - m_BaseCPUHandle.ptr) / m_DescriptorSize);
    m_AvailableDescriptors.push_back({ offset, allocation.GetDescriptorCount() });

    // Merge adjacent free blocks
    std::sort(m_AvailableDescriptors.begin(), m_AvailableDescriptors.end());
    for (auto it = m_AvailableDescriptors.begin(); it != m_AvailableDescriptors.end() - 1;)
    {
        if (it->first + it->second == (it + 1)->first)
        {
            it->second += (it + 1)->second;
            m_AvailableDescriptors.erase(it + 1);
        }
        else
        {
            ++it;
        }
    }
}

}