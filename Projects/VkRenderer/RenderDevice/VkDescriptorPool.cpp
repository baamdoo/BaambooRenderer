#include "RendererPch.h"
#include "VkDescriptorPool.h"
#include "VkDescriptorSet.h"

namespace vk
{

DescriptorPool::DescriptorPool(RenderContext& context, std::vector< VkDescriptorPoolSize >&& poolSizes, u32 maxSets, VkDescriptorPoolCreateFlags flags)
	: m_RenderContext(context)
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
	VK_CHECK(vkCreateDescriptorPool(m_RenderContext.vkDevice(), &poolInfo, nullptr, &m_vkDescriptorPool));
}

DescriptorPool::~DescriptorPool()
{
	for (auto& [_, pSet] : m_DescriptorSetCache)
	{
		RELEASE(pSet);
	}
	vkDestroyDescriptorPool(m_RenderContext.vkDevice(), m_vkDescriptorPool, nullptr);
}

DescriptorSet& DescriptorPool::AllocateSet(VkDescriptorSetLayout vkSetLayout)
{
	assert(vkSetLayout);

	auto it = m_DescriptorSetCache.find(vkSetLayout);
	if (it != m_DescriptorSetCache.end())
	{
		return *it->second;
	}

	auto pDescriptorSet = new DescriptorSet(m_RenderContext);
	m_DescriptorSetCache.emplace(vkSetLayout, pDescriptorSet);
	
	return pDescriptorSet->Allocate(vkSetLayout, m_vkDescriptorPool);
}

void DescriptorPool::Reset()
{
	VK_CHECK(vkResetDescriptorPool(m_RenderContext.vkDevice(), m_vkDescriptorPool, 0));
}

} // namespace vk