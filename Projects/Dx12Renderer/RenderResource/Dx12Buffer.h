#pragma once
#include "Dx12Resource.h"

namespace dx12
{

class Buffer : public Resource
{
using Super = Resource;

public:
    struct CreationInfo : public ResourceCreationInfo
    {
        u32 count;
        u64 elementSizeInBytes;
    };

    Buffer(RenderDevice& device, const std::wstring& name);
    Buffer(RenderDevice& device, const std::wstring& name, CreationInfo&& info);
    virtual ~Buffer() = default;

    [[nodiscard]]
    inline u64 GetSizeInBytes() const { return m_Count * m_ElementSize; }
    [[nodiscard]]
    inline u32 GetBufferCount() const { return m_Count; }
    [[nodiscard]]
    inline u64 GetElementSize() const { return m_ElementSize; }

protected:
    u32 m_Count;
    u64 m_ElementSize;
};

class VertexBuffer final : public Buffer
{
using Super = Buffer;

public:
    VertexBuffer(RenderDevice& device, const std::wstring& name, CreationInfo&& info);
    virtual ~VertexBuffer() = default;

    [[nodiscard]]
    const D3D12_VERTEX_BUFFER_VIEW& GetBufferView() const { return m_d3d12BufferView; }

private:
    D3D12_VERTEX_BUFFER_VIEW m_d3d12BufferView;
};

class IndexBuffer final : public Buffer
{
using Super = Buffer;

public:
    IndexBuffer(RenderDevice& device, const std::wstring& name, CreationInfo&& info);
    virtual ~IndexBuffer() = default;

    [[nodiscard]]
    const D3D12_INDEX_BUFFER_VIEW& GetBufferView() const { return m_d3d12BufferView; }

private:
    D3D12_INDEX_BUFFER_VIEW m_d3d12BufferView;
};

class ConstantBuffer : public Buffer
{
using Super = Buffer;

public:
    ConstantBuffer(RenderDevice& device, const std::wstring& name, CreationInfo&& info);
    virtual ~ConstantBuffer();

    [[nodiscard]]
    D3D12_CPU_DESCRIPTOR_HANDLE GetBufferView() const { return m_CBVAllocation.GetCPUHandle(); }
    [[nodiscard]]
    u8* GetSystemMemoryAddress() const { return m_pSystemMemory; }

private:
    DescriptorAllocation m_CBVAllocation = {};
    u8* m_pSystemMemory = nullptr;
};

class StructuredBuffer : public Buffer
{
using Super = Buffer;

public:
    StructuredBuffer(RenderDevice& device, const std::wstring& name, CreationInfo&& info);
    virtual ~StructuredBuffer();

    [[nodiscard]]
    D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView() const { return m_SRVAllocation.GetCPUHandle(); }
    [[nodiscard]]
    D3D12_CPU_DESCRIPTOR_HANDLE GetUnorederedAccessView() const { return m_UAVAllocation.GetCPUHandle(); }
    [[nodiscard]]
    u8* GetSystemMemoryAddress() const { return m_pSystemMemory; }

private:
    DescriptorAllocation m_SRVAllocation = {};
    DescriptorAllocation m_UAVAllocation = {};

    u8* m_pSystemMemory = nullptr;
};

}