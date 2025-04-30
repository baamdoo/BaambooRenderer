#pragma once
#include "VkResource.h"

namespace vk
{

class Buffer : public Resource< Buffer >
{
using Super = Resource< Buffer >;

public:
    struct CreationInfo
    {
        u32  count;
        u64  elementSize;
        bool bMap;

        VmaMemoryUsage     memoryUsage = VMA_MEMORY_USAGE_AUTO;
        VkBufferUsageFlags bufferUsage;
    };

    [[nodiscard]]
    inline VkBuffer vkBuffer() const { return m_vkBuffer; }
    [[nodiscard]]
    inline void* MappedMemory() const { assert(m_vmaAllocationInfo.pMappedData); return m_vmaAllocationInfo.pMappedData; }

    [[nodiscard]]
    inline u64 SizeInBytes() const { return m_count * m_elementSize; }
    [[nodiscard]]
    inline u32 BufferCount() const { return m_count; }
    [[nodiscard]]
    inline u64 ElementSize() const { return m_elementSize; }

protected:
    template< typename T >
    friend class ResourcePool;
    friend class ResourceManager;

    Buffer(RenderContext& context, std::wstring_view name);
    Buffer(RenderContext& context, std::wstring_view name, CreationInfo&& info);
    virtual ~Buffer();

private:
    u32 m_count;
    u32 m_elementSize;

    VkBuffer m_vkBuffer = VK_NULL_HANDLE;
};

}