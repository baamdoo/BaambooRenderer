#pragma once
#include "VkResource.h"

namespace vk
{

class VulkanSampler;

class VulkanTexture : public render::Texture, public VulkanResource< VulkanTexture >
{
public:
	// **
	// Texture resource state
	// State : VkImageLayout
	// Subresource : baseMip + mipLevels for high 32-bits & baseLayer + layerCount for low 32-bits
	// **
	struct Subresource
	{
		Subresource() : Subresource(0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS) {}
		Subresource(u32 baseMip, u32 mipLevels, u32 baseLayer, u32 layerCount)
			: baseMip(baseMip), mipLevels(mipLevels), baseLayer(baseLayer), arrayLayers(layerCount) {}
		Subresource(VkImageSubresourceRange range)
			: Subresource(range.baseMipLevel, range.levelCount, range.baseArrayLayer, range.layerCount) {}
		bool operator==(const Subresource& other) const { return baseMip == other.baseMip && mipLevels == other.mipLevels && baseLayer == other.baseLayer && arrayLayers == other.arrayLayers; }
		bool operator<(const Subresource& other) const {
			return std::tie(baseMip, mipLevels, baseLayer, arrayLayers) < std::tie(other.baseMip, other.baseLayer, other.baseLayer, other.arrayLayers);
		}

		u32 baseMip;
		u32 mipLevels;
		u32 baseLayer;
		u32 arrayLayers;
	};
	inline static const Subresource VK_ALL_SUBRESOURCES = Subresource();

	struct State
	{
		bool operator==(const State& other) const { return access == other.access && stage == other.stage && layout == other.layout; }

		VkAccessFlags2        access;
		VkPipelineStageFlags2 stage;
		VkImageLayout         layout;
	};
	struct ResourceState
	{
		ResourceState() = default;
		explicit ResourceState(State state_)
			: state(state_) {}

		// iterator
		auto begin() const noexcept { return subresourceStates.begin(); }
		auto end() const noexcept { return subresourceStates.end(); }

		[[nodiscard]]
		bool IsValid() const { return state.layout != VK_IMAGE_LAYOUT_UNDEFINED; }

		void SetSubresourceState(State state_, Subresource subresource)
		{
			if (subresource == VK_ALL_SUBRESOURCES)
			{
				state = state_;
				subresourceStates.clear();
			}
			else
			{
				subresourceStates[subresource] = state_;
			}
		}

		[[nodiscard]]
		State GetSubresourceState(Subresource subresource = VK_ALL_SUBRESOURCES) const
		{
			State state_ = state;

			const auto iter = subresourceStates.find(subresource);
			if (iter != subresourceStates.end())
				state_ = iter->second;

			return state_;
		}

		State                          state = {};
		std::map< Subresource, State > subresourceStates;
	};

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

	inline const ResourceState& GetState() const { return m_CurrentState; }
	void SetState(State state, Subresource subresource = VK_ALL_SUBRESOURCES) { m_CurrentState.SetSubresourceState(state, subresource); }

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

	ResourceState m_CurrentState = {};
};

} // namespace vk