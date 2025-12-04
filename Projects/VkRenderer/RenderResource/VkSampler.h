#pragma once
#include "VkResource.h"

namespace vk
{

class VulkanSampler : public render::Sampler, public VulkanResource< VulkanSampler >
{
public:
    static Arc< VulkanSampler > Create(VkRenderDevice& rd, const char* name, CreationInfo&& info);

    static Arc< VulkanSampler > CreateLinearRepeat(VkRenderDevice& device, const char* name = "LinearRepeat");
    static Arc< VulkanSampler > CreateLinearClamp(VkRenderDevice& device, const char* name = "LinearClamp");
    static Arc< VulkanSampler > CreatePointRepeat(VkRenderDevice& device, const char* name = "PointRepeat");
    static Arc< VulkanSampler > CreatePointClamp(VkRenderDevice& device, const char* name = "PointClamp");
    static Arc< VulkanSampler > CreateLinearClampCmp(VkRenderDevice& device, const char* name = "Shadow");

    VulkanSampler(VkRenderDevice& rd, const char* name, CreationInfo&& info);
    virtual ~VulkanSampler();

	[[nodiscard]]
	inline VkSampler vkSampler() const { return m_vkSampler; }

private:
	VkSampler m_vkSampler = VK_NULL_HANDLE;
};

} // namespace vk