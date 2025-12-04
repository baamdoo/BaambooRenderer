#pragma once
#include "Dx12Resource.h"

namespace dx12
{

enum class eBufferType
{
    None,
    Vertex,
    Index,
    Constant,
    Structured
};

class Dx12Buffer : public render::Buffer, public Dx12Resource
{
public:
    static Arc< Dx12Buffer > Create(Dx12RenderDevice& rd, const char* name, CreationInfo&& desc);
    static Arc< Dx12Buffer > CreateEmpty(Dx12RenderDevice& rd, const char* name);

    Dx12Buffer(Dx12RenderDevice& rd, const char* name);
    Dx12Buffer(Dx12RenderDevice& rd, const char* name, CreationInfo&& info, eBufferType type = eBufferType::None);
    virtual ~Dx12Buffer() = default;

    virtual void Resize(u64 sizeInBytes, bool bReset = false) override;

    eBufferType GetType() const { return m_Type; }
    virtual u64 SizeInBytes() const { return m_Count * m_ElementSize; }
    inline u32 GetBufferCount() const { return m_Count; }
    inline u64 GetElementSize() const { return m_ElementSize; }

    [[nodiscard]]
    u8* GetSystemMemoryAddress() const { return m_pSystemMemory; }

protected:
    eBufferType m_Type = eBufferType::None;

    u32 m_Count;
    u64 m_ElementSize;
    u8* m_pSystemMemory = nullptr;
};

class Dx12VertexBuffer final : public Dx12Buffer
{
using Super = Dx12Buffer;
public:
    static Arc< Dx12VertexBuffer > Create(Dx12RenderDevice& rd, const char* name, u32 numVertices);

    Dx12VertexBuffer(Dx12RenderDevice& rd, const char* name, u32 numVertices);
    virtual ~Dx12VertexBuffer() = default;

    [[nodiscard]]
    const D3D12_VERTEX_BUFFER_VIEW& GetBufferView() const { return m_d3d12BufferView; }

private:
    D3D12_VERTEX_BUFFER_VIEW m_d3d12BufferView;
};

class Dx12IndexBuffer final : public Dx12Buffer
{
using Super = Dx12Buffer;
public:
    static Arc< Dx12IndexBuffer > Create(Dx12RenderDevice& rd, const char* name, u32 numIndices);

    Dx12IndexBuffer(Dx12RenderDevice& rd, const char* name, u32 numIndices);
    virtual ~Dx12IndexBuffer() = default;

    [[nodiscard]]
    const D3D12_INDEX_BUFFER_VIEW& GetBufferView() const { return m_d3d12BufferView; }

private:
    D3D12_INDEX_BUFFER_VIEW m_d3d12BufferView;
};

class Dx12ConstantBuffer : public Dx12Buffer
{
using Super = Dx12Buffer;
public:
    static Arc< Dx12ConstantBuffer > Create(Dx12RenderDevice& rd, const char* name, u64 sizeInBytes, RenderFlags additionalUsage = 0);

    Dx12ConstantBuffer(Dx12RenderDevice& rd, const char* name, u64 sizeInBytes, RenderFlags additionalUsage = 0);
    virtual ~Dx12ConstantBuffer();

    [[nodiscard]]
    D3D12_CPU_DESCRIPTOR_HANDLE GetBufferView() const { return m_CBVAllocation.GetCPUHandle(); }

private:
    DescriptorAllocation m_CBVAllocation = {};
};

class Dx12StructuredBuffer : public Dx12Buffer
{
using Super = Dx12Buffer;
public:
    static Arc< Dx12StructuredBuffer > Create(Dx12RenderDevice& rd, const char* name, u64 sizeInBytes, RenderFlags additionalUsage = 0);

    Dx12StructuredBuffer(Dx12RenderDevice& rd, const char* name, u64 sizeInBytes, RenderFlags additionalUsage = 0);
    virtual ~Dx12StructuredBuffer();

    D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView() const { return m_SRVAllocation.GetCPUHandle(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetUnorderedAccessView() const { return m_UAVAllocation.GetCPUHandle(); }
    u32 GetShaderResourceHandle(u32 offset = 0) const { return m_SRVAllocation.Index(offset); }
    u32 GetUnorderedAccessHandle(u32 offset = 0) const { return m_UAVAllocation.Index(offset); }

private:
    DescriptorAllocation m_SRVAllocation = {};
    DescriptorAllocation m_UAVAllocation = {};
};

}