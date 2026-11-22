#pragma once
#include "RenderResource/VkBuffer.h"

namespace vk
{

//-------------------------------------------------------------------------
// Dynamic Buffer-Allocator
//     - resource visibility : device-host
//     - allocate additional buffer by new buffer creation
//-------------------------------------------------------------------------
class DynamicBufferAllocator
{
public:
    struct Allocation
	{
        Arc< VulkanBuffer > pBuffer;
        u64                 offsetInBytes;
        void*               cpuHandle;
    };

    DynamicBufferAllocator(VkRenderDevice& rd, VkDeviceSize pageSize = _64KB);
    ~DynamicBufferAllocator();

    [[nodiscard]]
    Allocation Allocate(VkDeviceSize sizeInBytes);
    void Reset();

private:
    struct Page
    {
        Page(VkRenderDevice& rd, VkDeviceSize sizeInBytes);
        ~Page();

        [[nodiscard]]
        Allocation Allocate(VkDeviceSize sizeInBytes, VkDeviceSize alignment);
        void Reset();

        [[nodiscard]]
        bool HasSpace(VkDeviceSize sizeInBytes, VkDeviceSize alignment) const;

    private:
        VkRenderDevice& m_RenderDevice;

        Arc< VulkanUniformBuffer > m_pBuffer;

        u64 m_OffsetInBytes = 0;
    };

    Page* RequestPage();

private:
    VkRenderDevice& m_RenderDevice;

    std::vector< Page* > m_pPages;
    std::deque< Page* >  m_pAvailablePages;

    Page* m_pCurrentPage = nullptr;

    u64 m_Alignment = 0;
    u64 m_MaxPageSize = 0;
};

} // namespace vk
