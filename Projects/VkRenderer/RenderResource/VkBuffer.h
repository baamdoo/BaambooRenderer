#pragma once
#include "VkResource.h"

namespace vk
{

class VulkanBuffer : public render::Buffer, public VulkanResource< VulkanBuffer >
{
public:
    static Arc< VulkanBuffer > Create(VkRenderDevice& rd, const std::string& name, CreationInfo&& desc);

    VulkanBuffer(VkRenderDevice& rd, const std::string& name, CreationInfo&& info);
    virtual ~VulkanBuffer();

    virtual void Resize(u64 sizeInBytes, bool bReset = false) override;

    [[nodiscard]]
    inline VkBuffer vkBuffer() const { return m_vkBuffer; }

    [[nodiscard]]
    inline u64 SizeInBytes() const { return m_CreationInfo.sizeInBytes; }

protected:
    VkBuffer        m_vkBuffer      = VK_NULL_HANDLE;
    VkDeviceAddress m_DeviceAddress = 0;
};

class VulkanIndexBuffer : public VulkanBuffer 
{
using Super = VulkanBuffer;
public:
    static Arc< VulkanIndexBuffer > Create(VkRenderDevice& rd, const std::string& name, u32 numIndices, VkIndexType type);

    VulkanIndexBuffer(VkRenderDevice& rd, const std::string& name, u32 numIndices, VkIndexType type);

    u32 GetIndexCount() const { return m_IndexCount; }
    u32 GetIndexSize() const { return m_IndexType == VK_INDEX_TYPE_UINT8_KHR ? 1 : m_IndexType == VK_INDEX_TYPE_UINT16 ? 2 : 4; }
    VkIndexType GetIndexType() const { return m_IndexType; }

private:
    u32 m_IndexCount = 0;

    VkIndexType m_IndexType;
};

class VulkanUniformBuffer : public VulkanBuffer 
{
using Super = VulkanBuffer;
public:
    static Arc< VulkanUniformBuffer > Create(VkRenderDevice& rd, const std::string& name, u64 sizeInBytes, VkBufferUsageFlags usage = 0);

    VulkanUniformBuffer(VkRenderDevice& rd, const std::string& name, u64 sizeInBytes, VkBufferUsageFlags additionalUsage);

    [[nodiscard]]
    inline void* MappedMemory() const { assert(m_AllocationInfo.pMappedData); return m_AllocationInfo.pMappedData; }
};

class VulkanStorageBuffer : public VulkanBuffer
{
using Super = VulkanBuffer;
public:
    static Arc< VulkanStorageBuffer > Create(VkRenderDevice& rd, const std::string& name, u64 sizeInBytes, VkBufferUsageFlags usage = 0);

    VulkanStorageBuffer(VkRenderDevice& rd, const std::string& name, u64 sizeInBytes, VkBufferUsageFlags additionalUsage);
};

}