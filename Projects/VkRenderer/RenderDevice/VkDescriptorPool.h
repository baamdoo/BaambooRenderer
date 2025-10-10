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
	DescriptorPool(VkRenderDevice& rd, std::vector< VkDescriptorPoolSize >&& poolSizes, u32 maxSets, VkDescriptorPoolCreateFlags flags = 0);
	~DescriptorPool();

	[[nodiscard]]
	DescriptorSet& AllocateSet(VkDescriptorSetLayout vkSetLayout);
	void Reset();

	[[nodiscard]]
	VkDescriptorPool vkDescriptorPool() const { return m_vkDescriptorPool; }

private:
	VkRenderDevice& m_RenderDevice;

	VkDescriptorPool            m_vkDescriptorPool;
	u32                         m_MaxSets = 0;
	VkDescriptorPoolCreateFlags m_Flags = 0;

	std::unordered_map< VkDescriptorSetLayout, DescriptorSet* > m_DescriptorSetCache;
};

} // namespace vk