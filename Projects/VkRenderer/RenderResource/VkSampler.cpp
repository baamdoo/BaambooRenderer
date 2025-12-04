#include "RendererPch.h"
#include "VkSampler.h"

namespace vk
{

using namespace render;

#define VK_SAMPLER_FILTER(mode) ConvertToVkSamplerFilter(mode)
VkFilter ConvertToVkSamplerFilter(eFilterMode filter)
{
    switch (filter)
    {
        case eFilterMode::Point:
            return VK_FILTER_NEAREST;
        case eFilterMode::Linear:
            return VK_FILTER_LINEAR;
        case eFilterMode::Anisotropic:
            return VK_FILTER_CUBIC_IMG;
    }

    assert(false && "Invalid filter mode!");
    return VK_FILTER_MAX_ENUM;
}

#define VK_SAMPLER_MIPMAP(mode) ConvertToVkSamplerMipMap(mode)
VkSamplerMipmapMode ConvertToVkSamplerMipMap(eMipmapMode mode)
{
    switch (mode)
    {
    case eMipmapMode::Nearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case eMipmapMode::Linear:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    assert(false && "Invalid mipmap mode!");
    return VK_SAMPLER_MIPMAP_MODE_MAX_ENUM;
}

#define VK_SAMPLER_ADDRESS(mode) ConvertToVkSamplerAddressMode(mode)
VkSamplerAddressMode ConvertToVkSamplerAddressMode(eAddressMode mode)
{
    switch (mode)
    {
    case eAddressMode::Wrap:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case eAddressMode::MirrorRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case eAddressMode::ClampEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case eAddressMode::ClampBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case eAddressMode::MirrorClampEdge:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    }

    assert(false && "Invalid address mode!");
    return VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
}

#define VK_SAMPLER_BORDERCOLOR(color) ConvertToVkSamplerBorderColor(color)
VkBorderColor ConvertToVkSamplerBorderColor(eBorderColor color)
{
    switch (color)
    {
    case eBorderColor::TransparentBlack_Float: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case eBorderColor::TransparentBlack_Int  : return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    case eBorderColor::OpaqueBlack_Float     : return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case eBorderColor::OpaqueBlack_Int       : return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    case eBorderColor::OpaqueWhite_Float     : return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    case eBorderColor::OpaqueWhite_Int       : return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    }

    assert(false && "Invalid boder color!");
    return VK_BORDER_COLOR_MAX_ENUM;
}


Arc< VulkanSampler > VulkanSampler::Create(VkRenderDevice& rd, const char* name, CreationInfo&& info)
{
	return MakeArc< VulkanSampler >(rd, name, std::move(info));
}

Arc< VulkanSampler > VulkanSampler::CreateLinearRepeat(VkRenderDevice& rd, const char* name) {
    return Create(rd, name,
        {
            .filter      = eFilterMode::Linear,
            .addressMode = eAddressMode::Wrap
        });
}

Arc< VulkanSampler > VulkanSampler::CreateLinearClamp(VkRenderDevice& rd, const char* name)
{
    return Create(rd, name,
        {
            .filter      = eFilterMode::Linear,
            .addressMode = eAddressMode::ClampEdge
        });
}

Arc< VulkanSampler > VulkanSampler::CreatePointRepeat(VkRenderDevice& rd, const char* name)
{
    return Create(rd, name,
        {
            .filter        = eFilterMode::Point,
            .addressMode   = eAddressMode::Wrap,
            .maxAnisotropy = 0.0f
        });
}

Arc< VulkanSampler > VulkanSampler::CreatePointClamp(VkRenderDevice& rd, const char* name)
{
    return Create(rd, name,
        {
            .filter        = eFilterMode::Point,
            .addressMode   = eAddressMode::ClampEdge,
            .maxAnisotropy = 0.0f
        });
}

Arc< VulkanSampler > VulkanSampler::CreateLinearClampCmp(VkRenderDevice& rd, const char* name)
{
    return Create(rd, name,
        {
            .filter        = eFilterMode::Linear,
            .addressMode   = eAddressMode::ClampBorder,
            .maxAnisotropy = 0.0f,
            .compareOp     = eCompareOp::LessEqual,
            .borderColor   = eBorderColor::OpaqueWhite_Float
        });
}

VulkanSampler::VulkanSampler(VkRenderDevice& rd, const char* name, CreationInfo&& info)
    : render::Sampler(name, std::move(info))
    , VulkanResource(rd, name)
{
	VkSamplerCreateInfo createInfo = {};
	createInfo.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	createInfo.minFilter        = createInfo.magFilter = VK_SAMPLER_FILTER(m_CreationInfo.filter);
	createInfo.mipmapMode       = VK_SAMPLER_MIPMAP(m_CreationInfo.mipmapMode);
	createInfo.addressModeU     = 
        createInfo.addressModeV = createInfo.addressModeW = VK_SAMPLER_ADDRESS(m_CreationInfo.addressMode);
	createInfo.mipLodBias       = m_CreationInfo.mipLodBias;
	createInfo.anisotropyEnable = m_CreationInfo.maxAnisotropy > 0.0f;
	createInfo.maxAnisotropy    = m_CreationInfo.maxAnisotropy;
	createInfo.compareEnable    = VK_COMPAREOP(m_CreationInfo.compareOp) > VK_COMPARE_OP_NEVER;
	createInfo.compareOp        = VK_COMPAREOP(m_CreationInfo.compareOp);
	createInfo.minLod           = m_CreationInfo.minLod;
	createInfo.maxLod           = m_CreationInfo.maxLod;
	createInfo.borderColor      = VK_SAMPLER_BORDERCOLOR(m_CreationInfo.borderColor);
	VK_CHECK(vkCreateSampler(m_RenderDevice.vkDevice(), &createInfo, nullptr, &m_vkSampler));

    SetDeviceObjectName((u64)m_vkSampler, VK_OBJECT_TYPE_SAMPLER);
}

VulkanSampler::~VulkanSampler()
{
    if (m_vkSampler)
	    vkDestroySampler(m_RenderDevice.vkDevice(), m_vkSampler, nullptr);
}

} // namespace vk