#pragma once

namespace dx12
{

class DescriptorPool;

class DescriptorAllocation
{
public:
    DescriptorAllocation();
    DescriptorAllocation(DescriptorPool* pool, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, u32 numDescriptors);

    DescriptorAllocation(const DescriptorAllocation&) = delete;
    DescriptorAllocation& operator=(const DescriptorAllocation&) = delete;

    DescriptorAllocation(DescriptorAllocation&& other) noexcept;
    DescriptorAllocation& operator=(DescriptorAllocation&& other) noexcept;

    ~DescriptorAllocation();

    void Free();

public:
    bool IsValid() const { return m_NumDescriptors > 0; }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(u32 offset = 0) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(u32 offset = 0) const;
    u32 GetDescriptorCount() const { return m_NumDescriptors; }

private:
    DescriptorPool* m_pDescriptorPool = nullptr;

    D3D12_CPU_DESCRIPTOR_HANDLE m_CPUHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_GPUHandle = {};
    u32 m_NumDescriptors = 0;
};

}