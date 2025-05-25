#pragma once

namespace dx12
{

class StructuredBuffer;

//-------------------------------------------------------------------------
// Dynamic Buffer-Allocator
//     - resource visibility : device-host
//     - allocate additional buffer by new buffer creation
//-------------------------------------------------------------------------
class DynamicBufferAllocator
{
protected:
    friend class CommandList;

    explicit DynamicBufferAllocator(RenderContext& context, size_t pageSize = _2MB);
    virtual ~DynamicBufferAllocator();

public:
    struct Allocation
    {
        u8*                       CPUHandle;
        D3D12_GPU_VIRTUAL_ADDRESS GPUHandle;
    };

    Allocation Allocate(size_t sizeInBytes, size_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    void Reset();

public:
    inline SIZE_T GetPageSize() const { return m_MaxPageSize; }

private:
    struct Page
    {
        Page(RenderContext& context, size_t sizeInBytes);
        ~Page();

        Allocation Allocate(size_t sizeInBytes, size_t alignment);
        
        void Reset();
        
        bool HasSpace(size_t sizeInBytes, size_t alignment) const;

    private:
        RenderContext&  m_RenderContext;
        ID3D12Resource* m_d3d12Resource = nullptr;

        void*                     m_BaseCpuHandle = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS m_BaseGpuHandle;

        SIZE_T m_PageSize;
        SIZE_T m_Offset;
    };

    Page* RequestPage();

    RenderContext& m_RenderContext;

    using PagePool = std::deque< Page* >;
    PagePool m_PagePool;
    PagePool m_AvailablePages;

    Page* m_pCurrentPage = nullptr;

    SIZE_T m_MaxPageSize;
};


//-------------------------------------------------------------------------
// Static Buffer-Allocator
//     - allocate additional buffer by resizing (keep only a single buffer)
//-------------------------------------------------------------------------
class StaticBufferAllocator
{
public:
    StaticBufferAllocator(RenderContext& context, size_t bufferSize = _4MB);
    ~StaticBufferAllocator();

    struct Allocation
    {
        baamboo::ResourceHandle< StructuredBuffer > buffer;

        u64                       offset = 0;
        u64                       sizeInBytes = 0;
        D3D12_GPU_VIRTUAL_ADDRESS gpuHandle = 0;
    };

    [[nodiscard]]
    Allocation Allocate(u32 numElements, u64 elementSizeInBytes);
    void Reset();

    [[nodiscard]]
    u64 GetAllocatedSize() const { return m_Offset; }
    [[nodiscard]]
    StructuredBuffer* GetBuffer() const;

private:
    void Resize(size_t sizeInBytes);

private:
    RenderContext& m_RenderContext;

    baamboo::ResourceHandle< StructuredBuffer > m_Buffer;
    D3D12_GPU_VIRTUAL_ADDRESS                   m_BaseGpuHandle;

    u64 m_Size = 0;
    u64 m_Offset = 0;
    u64 m_Alignment = 0;
};

}