#pragma once
#include "Dx12Resource.h"

namespace dx12
{

class Buffer : public Resource
{
using Super = Resource;

public:
    using CreationInfo = ResourceCreationInfo;

    [[nodiscard]]
    inline u32 GetSizeInBytes() const { return m_Count * m_ElementSize; }
    [[nodiscard]]
    inline u32 GetBufferCount() const { return m_Count; }
    [[nodiscard]]
    inline u32 GetElementSize() const { return m_ElementSize; }

protected:
    template< typename T >
    friend class ResourcePool;
    friend class ResourceManager;

    Buffer(RenderContext& context, std::wstring_view name);
    Buffer(RenderContext& context, std::wstring_view name, const CreationInfo& info, u32 count, u32 elementSize, eResourceType type);
    virtual ~Buffer() = default;

protected:
    u32 m_Count;
    u32 m_ElementSize;
};

class VertexBuffer final : public Buffer
{
using Super = Buffer;

public:
    VertexBuffer(RenderContext& context, std::wstring_view name, const ResourceCreationInfo& info, u32 count, u32 elementSize);
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
    IndexBuffer(RenderContext& context, std::wstring_view name, const ResourceCreationInfo& info, u32 count, u32 elementSize);
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
    ConstantBuffer(RenderContext& context, std::wstring_view name, const ResourceCreationInfo& info, u32 count, u32 elementSize);
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
    StructuredBuffer(RenderContext& context, std::wstring_view name, const ResourceCreationInfo& info, u32 count, u32 elementSize);
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