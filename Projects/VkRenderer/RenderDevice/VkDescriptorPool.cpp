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
	for (auto& [_, pSet] : m_DescriptorSetCache)
	{
		RELEASE(pSet);
	}
	vkDestroyDescriptorPool(m_RenderDevice.vkDevice(), m_vkDescriptorPool, nullptr);
}

DescriptorSet& DescriptorPool::AllocateSet(VkDescriptorSetLayout vkSetLayout, u32* variableCounts)
{
	assert(vkSetLayout);

	auto it = m_DescriptorSetCache.find(vkSetLayout);
	if (it != m_DescriptorSetCache.end())
	{
		return *it->second;
	}

	auto pDescriptorSet = new DescriptorSet(m_RenderDevice);
	m_DescriptorSetCache.emplace(vkSetLayout, pDescriptorSet);
	
	return pDescriptorSet->Allocate(vkSetLayout, m_vkDescriptorPool, variableCounts);
}

void DescriptorPool::Reset()
{
	VK_CHECK(vkResetDescriptorPool(m_RenderDevice.vkDevice(), m_vkDescriptorPool, 0));
}

} // namespace vk