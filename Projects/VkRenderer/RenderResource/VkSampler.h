#pragma once
#include "VkResource.h"
#include "RenderCommon/RenderResources.h"

namespace vk
{

class VulkanSampler : public render::Sampler, public VulkanResource< VulkanSampler >
{
public:
    static Arc< VulkanSampler > Create(VkRenderDevice& rd, const std::string& name, CreationInfo&& info);

    static Arc< VulkanSampler > CreateLinearRepeat(VkRenderDevice& device, const std::string& name = "LinearRepeat");
    static Arc< VulkanSampler > CreateLinearClamp(VkRenderDevice& device, const std::string& name = "LinearClamp");
    static Arc< VulkanSampler > CreatePointRepeat(VkRenderDevice& device, const std::string& name = "PointRepeat");
    static Arc< VulkanSampler > CreatePointClamp(VkRenderDevice& device, const std::string& name = "PointClamp");
    static Arc< VulkanSampler > CreateLinearClampCmp(VkRenderDevice& device, const std::string& name = "Shadow");

    VulkanSampler(VkRenderDevice& rd, const std::string& name, CreationInfo&& info);
    virtual ~VulkanSampler();

	[[nodiscard]]
	inline VkSampler vkSampler() const { return m_vkSampler; }

private:
	VkSampler m_vkSampler = VK_NULL_HANDLE;
};

} // namespace vk