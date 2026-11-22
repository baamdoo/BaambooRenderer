#pragma once
#include "VkResource.h"

namespace vk
{

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
	inline VkImageView vkView() const { return m_vkImageView; }
    inline const VkImageCreateInfo& Desc() const { return m_Desc; }
	inline VkImageAspectFlags AspectMask() const { return m_AspectFlags; }
	VkClearValue ClearValue() const;
	u64 SizeInBytes() const;
	u32 MipLevels() const override { return m_Desc.mipLevels; }

	u32 GetShaderResourceHandle(u32 offset = 0) const override { return offset == 0 ? m_SrvSlot : kInvalidIndex; }
	u32 GetUnorderedAccessHandle(u32 mip = 0) const { return mip < m_UavSlots.size() ? m_UavSlots[mip] : kInvalidIndex; }

protected:
    void CreateImageAndView(const CreationInfo& info);
    void CreateShaderViews();
    void DestroyImageAndViews();
    VkImageViewCreateInfo GetViewDesc(const VkImageCreateInfo& imageDesc);

private:
    VkImage     m_vkImage     = VK_NULL_HANDLE;
    VkImageView m_vkImageView = VK_NULL_HANDLE;

    std::vector< VkImageView > m_vkPerMipViews; // Per-mip UAV views for compute write

    u32                m_SrvSlot = kInvalidIndex; // descriptor heap slots
    std::vector< u32 > m_UavSlots;                // one per mip, matches m_vkPerMipViews

    VkImageCreateInfo  m_Desc        = {};
	VkImageAspectFlags m_AspectFlags = 0;
};

} // namespace vk