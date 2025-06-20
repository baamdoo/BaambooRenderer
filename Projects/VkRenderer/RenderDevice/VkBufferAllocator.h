#pragma once

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
        VkBuffer     vkBuffer;
        VkDeviceSize offset;
        VkDeviceSize size;
        void*        cpuHandle;
    };

    DynamicBufferAllocator(RenderDevice& device, VkDeviceSize pageSize = _1MB);
    ~DynamicBufferAllocator();

    [[nodiscard]]
    Allocation Allocate(VkDeviceSize sizeInBytes);
    void Reset();

private:
    struct Page
    {
        Page(RenderDevice& device, VkDeviceSize sizeInBytes);
        ~Page();

        [[nodiscard]]
        Allocation Allocate(VkDeviceSize sizeInBytes, VkDeviceSize alignment);
        void Reset();

        [[nodiscard]]
        bool HasSpace(VkDeviceSize sizeInBytes, VkDeviceSize alignment) const;

        void Activate(bool bActive) { m_bActivated = bActive; }

    private:
        RenderDevice& m_RenderDevice;

        VkBuffer      m_vkBuffer = nullptr;
        VmaAllocation m_vmaAllocation = VK_NULL_HANDLE;

        void* m_BaseCpuHandle = nullptr;

        u64  m_Offset = 0;
        u64  m_PageSize = 0;
        bool m_bActivated = false;
    };

    Page* RequestPage();

private:
    RenderDevice& m_RenderDevice;

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
    StaticBufferAllocator(RenderDevice& device, VkDeviceSize bufferSize = _4MB, VkBufferUsageFlags usage = 0);
    ~StaticBufferAllocator();

    struct Allocation
    {
        VkBuffer        vkBuffer;
        u32             offset;
        VkDeviceSize    size;
        VkDeviceAddress gpuHandle;
    };

    [[nodiscard]]
    Allocation Allocate(u32 numElements, u64 elementSizeInBytes);
    void Reset();

    [[nodiscard]]
    VkBuffer vkBuffer() const { return m_vkBuffer; }
    [[nodiscard]]
    u64 GetAllocatedSize() const { return m_Offset; }

    [[nodiscard]]
    VkDescriptorBufferInfo GetDescriptorInfo(u64 offset = 0) const;

private:
    void Resize(VkDeviceSize sizeInBytes);

private:
    RenderDevice& m_RenderDevice;

    VkBuffer          m_vkBuffer = nullptr;
    VmaAllocation     m_vmaAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo m_AllocationInfo;

    VkDeviceAddress m_BaseGpuHandle;

    u64 m_Size      = 0;
    u64 m_Offset    = 0;
    u64 m_Alignment = 0;

    VkBufferUsageFlags m_UsageFlags = 0;
};

} // namespace vk