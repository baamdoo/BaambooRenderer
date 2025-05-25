#pragma once

namespace dx12
{

class DescriptorPool;

class DescriptorAllocation
{
public:
    DescriptorAllocation();
    DescriptorAllocation(DescriptorPool* pool, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, u32 numDescriptors, u32 descriptorIndex);

    DescriptorAllocation(const DescriptorAllocation&) = delete;
    DescriptorAllocation& operator=(const DescriptorAllocation&) = delete;

    DescriptorAllocation(DescriptorAllocation&& other) noexcept;
    DescriptorAllocation& operator=(DescriptorAllocation&& other) noexcept;

    ~DescriptorAllocation();

    void Free();

public:
    bool IsValid() const { return m_NumDescriptors > 0; }

    [[nodiscard]]
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(u32 offset = 0) const;
    [[nodiscard]]
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(u32 offset = 0) const;

    [[nodiscard]]
    u32 GetDescriptorCount() const { return m_NumDescriptors; }
    [[nodiscard]]
    u32 Index(u32 offset = 0) const { return m_DescriptorBaseIndex + offset; }

private:
    DescriptorPool* m_pDescriptorPool = nullptr;

    D3D12_CPU_DESCRIPTOR_HANDLE m_CPUHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_GPUHandle = {};
    u32 m_NumDescriptors = 0;
    u32 m_DescriptorBaseIndex = 0;
};

}