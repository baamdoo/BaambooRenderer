#pragma once
#include "VkResource.h"

namespace vk
{

class Buffer : public Resource
{
using Super = Resource;

public:
    struct CreationInfo
    {
        u64  sizeInBytes;
        bool bMap;

        VmaMemoryUsage     memoryUsage = VMA_MEMORY_USAGE_AUTO;
        VkBufferUsageFlags usage;
    };

    static Arc< Buffer > Create(RenderDevice& device, std::string_view name, CreationInfo&& desc);

    Buffer(RenderDevice& device, std::string_view name, CreationInfo&& info);
    virtual ~Buffer();

    void Resize(u64 sizeInBytes, bool bReset = false);

    [[nodiscard]]
    inline VkBuffer vkBuffer() const { return m_vkBuffer; }

    [[nodiscard]]
    inline u64 SizeInBytes() const { return m_CreationInfo.sizeInBytes; }

protected:
    CreationInfo m_CreationInfo = {};

    VkBuffer        m_vkBuffer = VK_NULL_HANDLE;
    VkDeviceAddress m_DeviceAddress = 0;
};

class IndexBuffer : public Buffer 
{
using Super = Buffer;

public:
    static Arc< IndexBuffer > Create(RenderDevice& device, std::string_view name, u32 numIndices, VkIndexType type);

    IndexBuffer(RenderDevice& device, std::string_view name, u32 numIndices, VkIndexType type);

    u32 GetIndexCount() const { return m_IndexCount; }
    u32 GetIndexSize() const { return m_IndexType == VK_INDEX_TYPE_UINT8_KHR ? 1 : m_IndexType == VK_INDEX_TYPE_UINT16 ? 2 : 4; }
    VkIndexType GetIndexType() const { return m_IndexType; }

private:
    u32 m_IndexCount = 0;

    VkIndexType m_IndexType;
};

class UniformBuffer : public Buffer 
{
using Super = Buffer;

public:
    static Arc< UniformBuffer > Create(RenderDevice& device, std::string_view name, u64 sizeInBytes, VkBufferUsageFlags usage = 0);

    UniformBuffer(RenderDevice& device, std::string_view name, u64 sizeInBytes, VkBufferUsageFlags additionalUsage);

    [[nodiscard]]
    inline void* MappedMemory() const { assert(m_AllocationInfo.pMappedData); return m_AllocationInfo.pMappedData; }
};

class StorageBuffer : public Buffer
{
using Super = Buffer;

public:
    static Arc< StorageBuffer > Create(RenderDevice& device, std::string_view name, u64 sizeInBytes, VkBufferUsageFlags usage = 0);

    StorageBuffer(RenderDevice& device, std::string_view name, u64 sizeInBytes, VkBufferUsageFlags additionalUsage);
};

}