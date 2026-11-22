#pragma once
#include "VkResource.h"

namespace vk
{

class VulkanSampler : public render::Sampler, public VulkanResource< VulkanSampler >
{
public:
    static Arc< VulkanSampler > Create(VkRenderDevice& rd, const char* name, CreationInfo&& info);

    VulkanSampler(VkRenderDevice& rd, const char* name, CreationInfo&& info);
    virtual ~VulkanSampler();

	[[nodiscard]]
	inline VkSampler vkSampler() const { return m_vkSampler; }

private:
	VkSampler m_vkSampler = VK_NULL_HANDLE;
};

} // namespace vk