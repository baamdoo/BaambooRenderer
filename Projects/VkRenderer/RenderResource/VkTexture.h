#pragma once
#include "VkResource.h"

namespace vk
{

class VulkanSampler;

class VulkanTexture : public render::Texture, public VulkanResource< VulkanTexture >
{
public:
	static Arc< VulkanTexture > Create(VkRenderDevice& rd, const char* name, CreationInfo&& desc);
	static Arc< VulkanTexture > CreateEmpty(VkRenderDevice& rd, const char* name);

	VulkanTexture(VkRenderDevice& rd, const char* name);
	VulkanTexture(VkRenderDevice& rd, const char* name, CreationInfo&& info);
	virtual ~VulkanTexture();

	void Resize(u32 width, u32 height, u32 depth);
	void SetResource(VkImage vkImage, VkImageView vkImageView, VkImageCreateInfo createInfo, VmaAllocation vmaAllocation, VmaAllocationInfo vmaAllocInfo, VkImageAspectFlags aspectMask);

	inline CreationInfo Info() const { return m_CreationInfo; }
    inline VkImage vkImage() const { return m_vkImage; }
	VkImageView vkView() const;
    inline const VkImageCreateInfo& Desc() const { return m_Desc; }
	inline VkImageAspectFlags AspectMask() const { return m_AspectFlags; }
	VkClearValue ClearValue() const;
	u64 SizeInBytes() const;

protected:
    void CreateImageAndView(const CreationInfo& info);
    VkImageViewCreateInfo GetViewDesc(const VkImageCreateInfo& imageDesc);

private:
    VkImage     m_vkImage     = VK_NULL_HANDLE;
    VkImageView m_vkImageView = VK_NULL_HANDLE;
    VkImageView m_vkImageSRV  = VK_NULL_HANDLE;
    VkImageView m_vkImageUAV  = VK_NULL_HANDLE;

    VkImageCreateInfo  m_Desc        = {};
	VkImageAspectFlags m_AspectFlags = 0;
};

} // namespace vk