#include "RendererPch.h"
#include "VkDescriptorSet.h"

namespace vk
{

DescriptorSet::DescriptorSet(RenderContext& context, VkDescriptorPool vkPool)
	: m_renderContext(context)
	, m_vkPoolBelonged(vkPool)
{
}

DescriptorSet::~DescriptorSet()
{
	vkDestroyDescriptorSetLayout(m_renderContext.vkDevice(), m_vkSetLayout, nullptr);
}

} // namespace vk