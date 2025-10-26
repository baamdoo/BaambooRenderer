#pragma once
#include "Dx12DescriptorAllocation.h"

namespace dx12
{

class DescriptorPool
{
public:
	DescriptorPool(Dx12RenderDevice& rd, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags, u32 maxDescriptorCount);
	~DescriptorPool();

    DescriptorAllocation Allocate(u32 numDescriptors);
    void Free(DescriptorAllocation& allocation);

public:
    u32 GetDescriptorSize() const { return m_DescriptorSize; }
    ID3D12DescriptorHeap* GetD3D12DescriptorHeap() const { return m_d3d12DescriptorHeap; }

private:
    Dx12RenderDevice& m_RenderDevice;

    ID3D12DescriptorHeap* m_d3d12DescriptorHeap;
    D3D12_DESCRIPTOR_HEAP_TYPE m_d3d12HeapType;

    u32 m_NumDescriptors;
    u32 m_DescriptorSize;

    D3D12_CPU_DESCRIPTOR_HANDLE m_BaseCPUHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE m_BaseGPUHandle;

    // < offset, numDescriptor >
    std::vector< std::pair< u32, u32 > > m_AvailableDescriptors;

    std::mutex m_Mutex;
};

}