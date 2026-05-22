#include "RendererPch.h"
#include "VkDescriptorPool.h"
#include "VkDescriptorSet.h"

namespace vk
{

DescriptorPool::DescriptorPool(VkRenderDevice& rd, std::vector< VkDescriptorPoolSize >&& poolSizes, u32 maxSets, VkDescriptorPoolCreateFlags flags)
	: m_RenderDevice(rd)
	, m_Flags(flags)
	, m_MaxSets(maxSets)
{
	// **
	// descriptor pool
	// **
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = m_Flags;
	poolInfo.maxSets = m_MaxSets;
	poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	VK_CHECK(vkCreateDescriptorPool(m_RenderDevice.vkDevice(), &poolInfo, nullptr, &m_vkDescriptorPool));
}

DescriptorPool::~DescriptorPool()
{
	for (auto& [_, descriptorSets] : m_DescriptorSetCache)
	{
		for (auto pSet : descriptorSets)
		{
			RELEASE(pSet);
		}
	}
	vkDestroyDescriptorPool(m_RenderDevice.vkDevice(), m_vkDescriptorPool, nullptr);
}

DescriptorSet& DescriptorPool::AllocateSet(VkDescriptorSetLayout vkSetLayout, u32* variableCounts, u32 cacheIndex)
{
	assert(vkSetLayout);

	auto& descriptorSets = m_DescriptorSetCache[vkSetLayout];
	if (descriptorSets.size() <= cacheIndex)
	{
		descriptorSets.resize(cacheIndex + 1, nullptr);
	}

	if (descriptorSets[cacheIndex])
	{
		return *descriptorSets[cacheIndex];
	}

	auto pDescriptorSet = new DescriptorSet(m_RenderDevice);
	descriptorSets[cacheIndex] = pDescriptorSet;
	
	return pDescriptorSet->Allocate(vkSetLayout, m_vkDescriptorPool, variableCounts);
}

void DescriptorPool::Reset()
{
	VK_CHECK(vkResetDescriptorPool(m_RenderDevice.vkDevice(), m_vkDescriptorPool, 0));
}

} // namespace vk
