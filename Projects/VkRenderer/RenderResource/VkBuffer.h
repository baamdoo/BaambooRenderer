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
    inline u64 SizeInBytes() const { return m_Count * m_ElementSizeInBytes; }
    [[nodiscard]]
    inline u32 BufferCount() const { return m_Count; }
    [[nodiscard]]
    inline u64 ElementSize() const { return m_ElementSizeInBytes; }

    virtual ~Buffer();

protected:
    template< typename T >
    friend class ResourcePool;
    friend class ResourceManager;

    Buffer(RenderContext& context, std::wstring_view name);
    Buffer(RenderContext& context, std::wstring_view name, CreationInfo&& info);

private:
    u32 m_Count;
    u64 m_ElementSizeInBytes;

    VkBuffer m_vkBuffer = VK_NULL_HANDLE;
};

class VertexBuffer : public Buffer
{
public:
    VertexBuffer(RenderContext& context, std::wstring_view name, CreationInfo&& info);
};

class IndexBuffer : public Buffer
{
public:
    IndexBuffer(RenderContext& context, std::wstring_view name, CreationInfo&& info);
};

}