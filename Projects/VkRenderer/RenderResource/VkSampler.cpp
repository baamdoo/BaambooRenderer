#include "RendererPch.h"
#include "VkSampler.h"

namespace vk
{

Arc< Sampler > Sampler::Create(RenderDevice& device, std::string_view name, CreationInfo&& info)
{
	return MakeArc< Sampler >(device, name, std::move(info));
}

Arc< Sampler > Sampler::CreateLinearRepeat(RenderDevice& device, std::string_view name) {
    return Create(device, name,
        {
            .filter      = VK_FILTER_LINEAR,
            .addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT
        });
}

Arc< Sampler > Sampler::CreateLinearClamp(RenderDevice& device, std::string_view name) 
{
    return Create(device, name,
        {
            .filter      = VK_FILTER_LINEAR,
            .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
        });
}

Arc< Sampler > Sampler::CreatePointRepeat(RenderDevice& device, std::string_view name) 
{
    return Create(device, name,
        {
            .filter        = VK_FILTER_NEAREST,
            .addressMode   = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .maxAnisotropy = 0.0f
        });
}

Arc< Sampler > Sampler::CreatePointClamp(RenderDevice& device, std::string_view name) 
{
    return Create(device, name,
        {
            .filter        = VK_FILTER_NEAREST,
            .addressMode   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxAnisotropy = 0.0f
        });
}

Arc< Sampler > Sampler::CreateLinearClampCmp(RenderDevice& device, std::string_view name)
{
    return Create(device, name,
        {
            .filter        = VK_FILTER_LINEAR,
            .addressMode   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .maxAnisotropy = 0.0f,
            .compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL,
            .borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
        });
}

Sampler::Sampler(RenderDevice& device, std::string_view name, CreationInfo&& info)
: Super(device, name, eResourceType::Sampler)
, m_CreationInfo(std::move(info))
{
	VkSamplerCreateInfo createInfo = {};
	createInfo.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	createInfo.minFilter        = createInfo.magFilter = m_CreationInfo.filter;
	createInfo.mipmapMode       = m_CreationInfo.mipmapMode;
	createInfo.addressModeU     = createInfo.addressModeV = createInfo.addressModeW = m_CreationInfo.addressMode;
	createInfo.mipLodBias       = m_CreationInfo.mipLodBias;
	createInfo.anisotropyEnable = m_CreationInfo.maxAnisotropy > 0.0f;
	createInfo.maxAnisotropy    = m_CreationInfo.maxAnisotropy;
	createInfo.compareEnable    = m_CreationInfo.compareOp > VK_COMPARE_OP_NEVER;
	createInfo.compareOp        = m_CreationInfo.compareOp;
	createInfo.minLod           = m_CreationInfo.minLod;
	createInfo.maxLod           = m_CreationInfo.maxLod;
	createInfo.borderColor      = m_CreationInfo.borderColor;
	VK_CHECK(vkCreateSampler(m_RenderDevice.vkDevice(), &createInfo, nullptr, &m_vkSampler));

    SetDeviceObjectName((u64)m_vkSampler, VK_OBJECT_TYPE_SAMPLER);
}

Sampler::~Sampler()
{
    if (m_vkSampler)
	    vkDestroySampler(m_RenderDevice.vkDevice(), m_vkSampler, nullptr);
}

} // namespace vk