#include "RendererPch.h"
#include "VkDescriptorSet.h"
#include "VkDescriptorPool.h"

namespace vk
{

DescriptorSet::DescriptorSet(RenderContext& context)
	: m_renderContext(context)
{
}

DescriptorSet::~DescriptorSet()
{
}

DescriptorSet& DescriptorSet::Allocate(VkDescriptorSetLayout vkSetLayout, VkDescriptorPool vkPool)
{
    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = vkPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &vkSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(m_renderContext.vkDevice(), &allocateInfo, &m_vkDescriptorSet));

    return *this;
}

void DescriptorSet::Reset()
{
    m_indexAllocator.clear();
}

DescriptorHandle DescriptorSet::StageDescriptor(const VkDescriptorImageInfo& imageInfo, u32 binding, VkDescriptorType descriptorType)
{
    return StageDescriptors({ imageInfo }, binding, descriptorType);
}

DescriptorHandle DescriptorSet::StageDescriptors(const std::vector< VkDescriptorImageInfo >& imageInfos, u32 binding, VkDescriptorType descriptorType)
{
    if (imageInfos.empty())
        return { INVALID_INDEX, 0 };

    auto index = m_indexAllocator.allocate();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_vkDescriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = descriptorType;
    descriptorWrite.descriptorCount = static_cast<u32>(imageInfos.size());
    descriptorWrite.pImageInfo = imageInfos.data();
    vkUpdateDescriptorSets(m_renderContext.vkDevice(), 1, &descriptorWrite, 0, nullptr);

    return { index, u32(imageInfos.size()) };
}

DescriptorHandle DescriptorSet::StageDescriptor(const VkDescriptorBufferInfo& bufferInfo, u32 binding, VkDescriptorType descriptorType)
{
    if (bufferInfo.range == 0)
    {
        return { INVALID_INDEX, 0 };
    }

    return StageDescriptors({ bufferInfo }, binding, descriptorType);
}

DescriptorHandle DescriptorSet::StageDescriptors(const std::vector< VkDescriptorBufferInfo >& bufferInfos, u32 binding, VkDescriptorType descriptorType)
{
    if (bufferInfos.empty())
        return { INVALID_INDEX, 0 };

    auto index = m_indexAllocator.allocate();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_vkDescriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = descriptorType;
    descriptorWrite.descriptorCount = static_cast<u32>(bufferInfos.size());
    descriptorWrite.pBufferInfo = bufferInfos.data();
    vkUpdateDescriptorSets(m_renderContext.vkDevice(), 1, &descriptorWrite, 0, nullptr);

    return { index, u32(bufferInfos.size()) };
}

} // namespace vk