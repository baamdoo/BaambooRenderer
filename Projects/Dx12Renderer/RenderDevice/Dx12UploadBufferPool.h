#pragma once

namespace dx12
{

class UploadBufferPool
{
protected:
    friend class CommandList;

    explicit UploadBufferPool(RenderContext& context, size_t pageSize = _2MB);
    virtual ~UploadBufferPool();

public:
    struct Allocation
    {
        u8* CPUHandle;
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
        RenderContext& m_RenderContext;
        ID3D12Resource* m_d3d12Resource = nullptr;

        void* m_BaseCPUHandle = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS m_BaseGPUHandle;

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

}