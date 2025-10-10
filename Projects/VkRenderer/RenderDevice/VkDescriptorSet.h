#pragma once
#include "FreeList.hpp"

namespace vk
{

class DescriptorPool;

struct DescriptorHandle
{
    u32 index;
    u32 count;
};

//-----------------------------------------------------------------------------
// DescriptorSet : wrapper of descriptor-set with index management
//-----------------------------------------------------------------------------
class DescriptorSet
{
public:
    DescriptorSet(VkRenderDevice& rd);
    ~DescriptorSet();

    DescriptorHandle StageDescriptor(const VkDescriptorImageInfo& imageInfo, u32 binding, VkDescriptorType descriptorType);
    DescriptorHandle StageDescriptors(const std::vector< VkDescriptorImageInfo >& imageInfos, u32 binding, VkDescriptorType descriptorType);
    DescriptorHandle StageDescriptor(const VkDescriptorBufferInfo& bufferInfo, u32 binding, VkDescriptorType descriptorType);
    DescriptorHandle StageDescriptors(const std::vector< VkDescriptorBufferInfo >& bufferInfos, u32 binding, VkDescriptorType descriptorType);

    DescriptorSet& Allocate(VkDescriptorSetLayout vkSetLayout, VkDescriptorPool vkPool);
    void Reset();

    [[nodiscard]]
    const VkDescriptorSet vkDescriptorSet() const { return m_vkDescriptorSet; }

private:
    VkRenderDevice& m_RenderDevice;

    VkDescriptorSet     m_vkDescriptorSet = VK_NULL_HANDLE;
    baamboo::FreeList<> m_IndexAllocator;
};

} // namespace vk