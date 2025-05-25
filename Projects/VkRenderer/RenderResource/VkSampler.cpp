#include "RendererPch.h"
#include "VkSampler.h"

namespace vk
{

Sampler::Sampler(RenderContext& context, std::wstring_view name, const CreationInfo& info)
	: Super(context, name)
{
	VkSamplerCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	createInfo.minFilter = info.interpolation == eSamplerInterpolation::Cubic ? 
		VK_FILTER_CUBIC_EXT : info.interpolation == eSamplerInterpolation::Nearest ? 
		VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	createInfo.magFilter = info.interpolation == eSamplerInterpolation::Cubic ? 
		VK_FILTER_CUBIC_EXT : info.interpolation == eSamplerInterpolation::Nearest ? 
		VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	createInfo.mipmapMode = info.interpolation == eSamplerInterpolation::Nearest ? 
		VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
	createInfo.addressModeU = createInfo.addressModeV = createInfo.addressModeW = info.type == eSamplerType::Mirrored ? 
		VK_SAMPLER_ADDRESS_MODE_REPEAT : info.type == eSamplerType::Repeat ? 
		VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.mipLodBias = info.mipLodBias;
	createInfo.anisotropyEnable = info.maxAnisotropy > 0.0f;
	createInfo.maxAnisotropy = info.maxAnisotropy;
	createInfo.compareEnable = info.compareOp > VK_COMPARE_OP_NEVER;
	createInfo.compareOp = info.compareOp;
	createInfo.minLod = 0;
	createInfo.maxLod = info.lod;
	createInfo.borderColor = info.borderColor;
	VK_CHECK(vkCreateSampler(m_renderContext.vkDevice(), &createInfo, nullptr, &m_vkSampler));
}

Sampler::~Sampler()
{
	vkDestroySampler(m_renderContext.vkDevice(), m_vkSampler, nullptr);
}

} // namespace vk