#pragma once
#include "VkResource.h"

namespace vk
{

class VulkanBuffer : public render::Buffer, public VulkanResource< VulkanBuffer >
{
public:
    static Arc< VulkanBuffer > Create(VkRenderDevice& rd, const char* name, CreationInfo&& desc);
    static Arc< VulkanBuffer > CreateEmpty(VkRenderDevice& rd, const char* name);

    VulkanBuffer(VkRenderDevice& rd, const char* name);
    VulkanBuffer(VkRenderDevice& rd, const char* name, CreationInfo&& info);
    virtual ~VulkanBuffer();

    virtual void Resize(u64 sizeInBytes, bool bReset = false) override;

    [[nodiscard]]
    inline VkBuffer vkBuffer() const { return m_vkBuffer; }
    [[nodiscard]]
    virtual u64 GetGPUVirtualAddress() const override { return m_DeviceAddress; }
    [[nodiscard]]
    virtual void* MappedMemory() const override { return m_AllocationInfo.pMappedData; }
    virtual void FlushMappedRange(u64 offsetInBytes, u64 sizeInBytes) const override;
    virtual void InvalidateMappedRange(u64 offsetInBytes, u64 sizeInBytes) const override;

    [[nodiscard]]
    inline u64 SizeInBytes() const { return m_CreationInfo.count * m_CreationInfo.elementSizeInBytes; }

    [[nodiscard]]
    virtual u32 GetShaderResourceHandle(u32 offset = 0) const override { return offset == 0 ? m_StorageSlot : kInvalidIndex; }

protected:
    VkBuffer        m_vkBuffer      = VK_NULL_HANDLE;
    VkDeviceAddress m_DeviceAddress = 0;
    u32             m_StorageSlot   = kInvalidIndex;
};

class VulkanUniformBuffer : public VulkanBuffer 
{
using Super = VulkanBuffer;
public:
    static Arc< VulkanUniformBuffer > Create(VkRenderDevice& rd, const char* name, u64 sizeInBytes, VkBufferUsageFlags2 usage = 0);

    VulkanUniformBuffer(VkRenderDevice& rd, const char* name, u64 sizeInBytes, VkBufferUsageFlags2 additionalUsage);
};

}