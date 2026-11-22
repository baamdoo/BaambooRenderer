#pragma once
#include "RenderResource/Dx12Buffer.h"

namespace dx12
{


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
        Arc< Dx12Buffer > pBuffer;
        u64               offsetInBytes;

        u8*               CPUHandle;
    };

    Allocation Allocate(size_t sizeInBytes, size_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    void Reset();

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

		Arc< Dx12Buffer > m_pBuffer;

        SIZE_T m_OffsetInBytes = 0;
    };

    Page* RequestPage();

    Dx12RenderDevice& m_RenderDevice;

    using PagePool = std::deque< Page* >;
    PagePool m_PagePool;
    PagePool m_AvailablePages;

    Page* m_pCurrentPage = nullptr;

    SIZE_T m_MaxPageSize;
};

}
