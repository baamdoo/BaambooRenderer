#pragma once

namespace vk
{

class DescriptorSet;

//-----------------------------------------------------------------------------
// DescriptorPool
//     - one-to-one correspondance with VkDescriptorPool
//-----------------------------------------------------------------------------
class DescriptorPool
{
public:
	DescriptorPool(RenderContext& context, std::vector< VkDescriptorPoolSize >&& poolSizes, u32 maxSets, VkDescriptorPoolCreateFlags flags = 0);
	~DescriptorPool();

	[[nodiscard]]
	DescriptorSet& AllocateSet(VkDescriptorSetLayout vkSetLayout);
	void Reset();

	[[nodiscard]]
	VkDescriptorPool vkDescriptorPool() const { return m_vkDescriptorPool; }

private:
	RenderContext& m_renderContext;

	VkDescriptorPool            m_vkDescriptorPool;
	u32                         m_maxSets = 0;
	VkDescriptorPoolCreateFlags m_flags = 0;

	std::unordered_map< VkDescriptorSetLayout, DescriptorSet* > m_descriptorSetCache;
};

} // namespace vk