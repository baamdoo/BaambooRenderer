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
        u64                 sizeInBytes;
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

        void Activate(bool bActive) { m_bActivated = bActive; }

    private:
        VkRenderDevice& m_RenderDevice;

        Arc< VulkanUniformBuffer > m_pBuffer;

        u64  m_OffsetInBytes = 0;
        bool m_bActivated    = false;
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


//-------------------------------------------------------------------------
// Static Buffer-Allocator
//     - allocate additional buffer by resizing (keep only a single buffer)
//-------------------------------------------------------------------------
class StaticBufferAllocator
{
public:
    StaticBufferAllocator(VkRenderDevice& rd, VkDeviceSize bufferSize = _4MB, VkBufferUsageFlags2 usage = 0);
    ~StaticBufferAllocator();

    struct Allocation
    {
        Arc< VulkanBuffer > pBuffer;
        u32                 offset;
        u64                 sizeInBytes;
        VkDeviceAddress     gpuHandle;
    };

    [[nodiscard]]
    Allocation Allocate(u32 numElements, u64 elementSizeInBytes);
    void Reset();

	[[nodiscard]]
    const Arc< VulkanBuffer >& GetAllocationBuffer() const { return m_pAllocatedBuffer; }
    [[nodiscard]]
    u64 GetAllocatedSize() const { return m_Offset; }

    [[nodiscard]]
    VkDescriptorBufferInfo GetDescriptorInfo(u64 offset = 0) const;

private:
    void Resize(VkDeviceSize sizeInBytes);

private:
    VkRenderDevice& m_RenderDevice;

    Arc< VulkanBuffer > m_pAllocatedBuffer;

    u64 m_Offset    = 0;
    u64 m_Alignment = 0;

    VkBufferUsageFlags2 m_UsageFlags = 0;
};

} // namespace vk