#pragma once

namespace dx12
{

class Dx12StructuredBuffer;

//-------------------------------------------------------------------------
// Dynamic Buffer-Allocator
//     - resource visibility : device-host
//     - allocate additional buffer by new buffer creation
//-------------------------------------------------------------------------
class DynamicBufferAllocator
{
public:
    explicit DynamicBufferAllocator(Dx12RenderDevice& rd, size_t pageSize = _2MB);
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
        Page(Dx12RenderDevice& rd, size_t sizeInBytes);
        ~Page();

        Allocation Allocate(size_t sizeInBytes, size_t alignment);
        
        void Reset();
        
        bool HasSpace(size_t sizeInBytes, size_t alignment) const;

    private:
        Dx12RenderDevice& m_RenderDevice;
        ID3D12Resource*   m_d3d12Resource = nullptr;

        void*                     m_BaseCpuHandle = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS m_BaseGpuHandle;

        SIZE_T m_PageSize;
        SIZE_T m_Offset;
    };

    Page* RequestPage();

    Dx12RenderDevice& m_RenderDevice;

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
    StaticBufferAllocator(Dx12RenderDevice& rd, const std::string& name, size_t bufferSize = _4MB);
    ~StaticBufferAllocator();

    struct Allocation
    {
        Arc< Dx12StructuredBuffer >   pBuffer;

        u64                       offset = 0;
        u64                       sizeInBytes = 0;
        D3D12_GPU_VIRTUAL_ADDRESS gpuHandle = 0;
    };

    [[nodiscard]]
    Allocation Allocate(u32 numElements, u64 elementSizeInBytes);
    void Reset();

    [[nodiscard]]
    u64 GetAllocatedSize() const { return m_OffsetInBytes; }
    [[nodiscard]]
    Arc< Dx12StructuredBuffer > GetBuffer() const { return m_pBuffer; }

private:
    void Resize(size_t sizeInBytes);

private:
    Dx12RenderDevice& m_RenderDevice;
    std::string       m_Name;

    Arc< Dx12StructuredBuffer > m_pBuffer;
    D3D12_GPU_VIRTUAL_ADDRESS   m_BaseGpuHandle;

    u64 m_SizeInBytes   = 0;
    u64 m_OffsetInBytes = 0;
    u64 m_Alignment     = 0;
};

}