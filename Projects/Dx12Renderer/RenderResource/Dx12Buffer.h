#pragma once
#include "Dx12Resource.h"

namespace dx12
{

enum class eBufferType
{
    None,
    Constant,
    Structured,
    Typed,
    Raw,
};

class Dx12Buffer : public render::Buffer, public Dx12Resource
{
public:
    static Arc< Dx12Buffer > Create(Dx12RenderDevice& rd, const char* name, CreationInfo&& desc);
    static Arc< Dx12Buffer > CreateEmpty(Dx12RenderDevice& rd, const char* name);

    Dx12Buffer(Dx12RenderDevice& rd, const char* name);
    Dx12Buffer(Dx12RenderDevice& rd, const char* name, CreationInfo&& info, eBufferType type = eBufferType::None);
    virtual ~Dx12Buffer();

    virtual void Resize(u64 sizeInBytes, bool bReset = false) override;

    virtual u64 SizeInBytes() const { return m_BufferSize; }
    virtual u64 GetGPUVirtualAddress() const override { return GpuAddress(); }

	[[nodiscard]]
    inline bool IsMapped() const { return m_pSystemMemory != nullptr; }
    [[nodiscard]]
    virtual void* MappedMemory() const override { return m_pSystemMemory; }
    virtual void FlushMappedRange(u64, u64) const override {}
    virtual void InvalidateMappedRange(u64, u64) const override {}

    virtual u32 GetShaderResourceHandle(u32 offset = 0) const override { return offset < m_SRVAllocation.GetDescriptorCount() ? m_SRVAllocation.Index(offset) : kInvalidIndex; }
    u32 GetUnorderedAccessHandle(u32 offset = 0) const { return offset < m_UAVAllocation.GetDescriptorCount() ? m_UAVAllocation.Index(offset) : kInvalidIndex; }

protected:
    virtual void CreateViews();
    void ReleaseViews();

protected:
    eBufferType m_Type = eBufferType::None;

    u32 m_Count;
    u64 m_ElementSize;
    u64 m_BufferSize;
    u8* m_pSystemMemory = nullptr;

    DescriptorAllocation m_SRVAllocation = {};
    DescriptorAllocation m_UAVAllocation = {};
};

class Dx12ConstantBuffer : public Dx12Buffer
{
using Super = Dx12Buffer;
public:
    static Arc< Dx12ConstantBuffer > Create(Dx12RenderDevice& rd, const char* name, u64 sizeInBytes, RenderFlags additionalUsage = 0);

    Dx12ConstantBuffer(Dx12RenderDevice& rd, const char* name, u64 sizeInBytes, RenderFlags additionalUsage = 0);
    virtual ~Dx12ConstantBuffer();

    void Reset();

    [[nodiscard]]
    D3D12_CPU_DESCRIPTOR_HANDLE GetConstantBufferViewCPU() const { return m_CBVAllocation.GetCPUHandle(); }

private:
    DescriptorAllocation m_CBVAllocation = {};
};

}
