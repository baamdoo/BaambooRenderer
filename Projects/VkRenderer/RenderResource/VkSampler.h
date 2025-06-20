#pragma once
#include "VkResource.h"

namespace vk
{

class Sampler : public Resource
{
using Super = Resource;

public:
	struct CreationInfo
	{
        VkFilter             filter        = VK_FILTER_LINEAR;
        VkSamplerMipmapMode  mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        VkSamplerAddressMode addressMode   = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        f32                  mipLodBias    = 0.0f;
        f32                  maxAnisotropy = 16.0f;
        VkCompareOp          compareOp     = VK_COMPARE_OP_NEVER;
        f32                  minLod        = 0.0f;
        f32                  maxLod        = VK_LOD_CLAMP_NONE;
        VkBorderColor        borderColor   = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	};

    static Arc< Sampler > Create(RenderDevice& device, const std::string& name, CreationInfo&& info);

    static Arc< Sampler > CreateLinearRepeat(RenderDevice& device, const std::string& name = "LinearRepeat");
    static Arc< Sampler > CreateLinearClamp(RenderDevice& device, const std::string& name = "LinearClamp");
    static Arc< Sampler > CreatePointRepeat(RenderDevice& device, const std::string& name = "PointRepeat");
    static Arc< Sampler > CreatePointClamp(RenderDevice& device, const std::string& name = "PointClamp");
    static Arc< Sampler > CreateLinearClampCmp(RenderDevice& device, const std::string& name = "Shadow");

    Sampler(RenderDevice& device, const std::string& name, CreationInfo&& info);
    virtual ~Sampler();

	[[nodiscard]]
	inline VkSampler vkSampler() const { return m_vkSampler; }

private:
    CreationInfo m_CreationInfo = {};

	VkSampler m_vkSampler = VK_NULL_HANDLE;
};

} // namespace vk