#pragma once

namespace vk
{

class UploadBufferPool
{
private:
    friend class CommandBuffer;
    UploadBufferPool(RenderContext& context, VkDeviceSize pageSize = _1MB);
    ~UploadBufferPool();

public:
    struct Allocation
	{
        VkBuffer     vkBuffer;
        VkDeviceSize offset;
        VkDeviceSize size;
        void*        cpuHandle;
    };

    [[nodiscard]]
    Allocation Allocate(VkDeviceSize sizeInBytes);
    void Reset();

private:
    struct Page
    {
        Page(RenderContext& context, VkDeviceSize sizeInBytes);
        ~Page();

        [[nodiscard]]
        Allocation Allocate(VkDeviceSize sizeInBytes, VkDeviceSize alignment);
        void Reset();

        [[nodiscard]]
        bool HasSpace(VkDeviceSize sizeInBytes, VkDeviceSize alignment) const;

        void Activate(bool bActive) { m_bActivated = bActive; }

    private:
        RenderContext& m_renderContext;

        VkBuffer      m_vkBuffer = nullptr;
        VmaAllocation m_vmaAllocation = VK_NULL_HANDLE;

        void* m_baseCpuHandle = nullptr;

        u64  m_offset = 0;
        u64  m_pageSize = 0;
        bool m_bActivated = false;
    };

    Page* RequestPage();

private:
    RenderContext& m_renderContext;

    std::vector< Page* > m_pPages;
    std::deque< Page* >  m_pAvailablePages;

    Page* m_pCurrentPage = nullptr;

    u64 m_alignment = 0;
    u64 m_maxPageSize = 0;
};

} // namespace vk