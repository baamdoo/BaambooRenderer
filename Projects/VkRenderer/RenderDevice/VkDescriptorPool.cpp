#include "RendererPch.h"
#include "VkDescriptorPool.h"
#include "VkDescriptorSet.h"

namespace vk
{

DescriptorPool::DescriptorPool(RenderContext& context, std::vector< VkDescriptorPoolSize >&& poolSizes, u32 maxSets, VkDescriptorPoolCreateFlags flags)
	: m_renderContext(context)
	, m_flags(flags)
	, m_maxSets(maxSets)
{
	// **
	// descriptor pool
	// **
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = m_flags;
	poolInfo.maxSets = m_maxSets;
	poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	VK_CHECK(vkCreateDescriptorPool(m_renderContext.vkDevice(), &poolInfo, nullptr, &m_vkDescriptorPool));
}

DescriptorPool::~DescriptorPool()
{
	for (auto& [_, pSet] : m_descriptorSetCache)
	{
		RELEASE(pSet);
	}
	vkDestroyDescriptorPool(m_renderContext.vkDevice(), m_vkDescriptorPool, nullptr);
}

DescriptorSet& DescriptorPool::AllocateSet(VkDescriptorSetLayout vkSetLayout)
{
	assert(vkSetLayout);

	auto it = m_descriptorSetCache.find(vkSetLayout);
	if (it != m_descriptorSetCache.end())
	{
		return *it->second;
	}

	auto pDescriptorSet = new DescriptorSet(m_renderContext);
	m_descriptorSetCache.emplace(vkSetLayout, pDescriptorSet);
	
	return pDescriptorSet->Allocate(vkSetLayout, m_vkDescriptorPool);
}

void DescriptorPool::Reset()
{
	VK_CHECK(vkResetDescriptorPool(m_renderContext.vkDevice(), m_vkDescriptorPool, 0));
}

} // namespace vk