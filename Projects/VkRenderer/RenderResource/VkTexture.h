#pragma once
#include "VkResource.h"

namespace vk
{

enum class eTextureType
{
    Texture1D = VK_IMAGE_TYPE_1D,
    Texture2D = VK_IMAGE_TYPE_2D,
	Texture3D = VK_IMAGE_TYPE_3D,
	TextureCube,
};

class Texture : public Resource< Texture >
{
using Super = Resource< Texture >;

public:
    struct CreationInfo
    {
        eTextureType type = eTextureType::Texture2D;
        VkExtent3D   resolution = {};
        VkFormat     format = VK_FORMAT_R8G8B8A8_UNORM;
		VkClearValue clearValue = { .color = { 0.0f, 0.0f, 0.0f, 0.0f} };

        u32  arrayLayers = 1;
        u32  sampleCount = 1;
        bool bFlipY = false;
        bool bGenerateMips = false;

        VmaMemoryUsage    memoryUsage = VMA_MEMORY_USAGE_AUTO;
        VkImageUsageFlags imageUsage;

		operator VkImageCreateInfo() const;
    };

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
		bool operator<(const Subresource& other) const { return baseMip < other.baseMip && mipLevels < other.mipLevels; } // tmp

		u32 baseMip;
		u32 mipLevels;
		u32 baseLayer;
		u32 arrayLayers;
	};
	inline static const Subresource ALL_SUBRESOURCES = Subresource();

	using State = VkImageLayout;
	struct ResourceState
	{
		ResourceState() = default;
		explicit ResourceState(State state_)
			: state(state_) {}

		// iterator
		auto begin() const noexcept { return subresourceStates.begin(); }
		auto end() const noexcept { return subresourceStates.end(); }

		[[nodiscard]]
		bool IsValid() const { return state != VK_IMAGE_LAYOUT_UNDEFINED; }

		// Force to set all subresource states equal
		void FlattenResourceState()
		{
			if (subresourceStates.empty())
				return;

			State state_ = subresourceStates.begin()->second;
			for (auto pair : subresourceStates)
				BB_ASSERT(state_ == pair.second, "All subresource states should be equal before flatten");

			SetSubresourceState(state_, ALL_SUBRESOURCES);
		}

		void SetSubresourceState(State state_, Subresource subresource)
		{
			if (subresource == ALL_SUBRESOURCES)
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
		State GetSubresourceState(Subresource subresource = ALL_SUBRESOURCES) const
		{
			State state_ = state;

			const auto iter = subresourceStates.find(subresource);
			if (iter != subresourceStates.end())
				state_ = iter->second;

			return state_;
		}

		State                          state;
		std::map< Subresource, State > subresourceStates;
	};

	void Resize(u32 width, u32 height, u32 depth);
	void SetResource(VkImage vkImage, VkImageView vkImageView, VmaAllocation vmaAllocation);

	[[nodiscard]]
	inline CreationInfo Info() const { return m_creationInfo; }
    [[nodiscard]]
    inline VkImage vkImage() const { return m_vkImage; }
    [[nodiscard]]
    inline VkImageView vkView() const { return m_vkImageView; }
    [[nodiscard]]
    inline const VkImageCreateInfo& Desc() const { return m_desc; }
	[[nodiscard]]
	inline const VkClearValue& ClearValue() const { return m_clearValue; }
	[[nodiscard]]
	u64 SizeInBytes() const;

	[[nodiscard]]
	inline const ResourceState& GetState() const { return m_currentState; }
	void SetState(State state, Subresource subresource = ALL_SUBRESOURCES) { m_currentState.SetSubresourceState(state, subresource); }

	void FlattenSubresourceStates() { m_currentState.FlattenResourceState(); }

protected:
	template< typename T >
	friend class ResourcePool;
	friend class ResourceManager;

	explicit Texture(RenderContext& context, std::wstring_view name);
	explicit Texture(RenderContext& context, std::wstring_view name, CreationInfo&& info);
	virtual ~Texture();

    void CreateImageAndView(const CreationInfo& info);
    VkImageViewCreateInfo GetViewDesc(const VkImageCreateInfo& imageDesc);

private:
    VkImage           m_vkImage = VK_NULL_HANDLE;
    VkImageView       m_vkImageView = VK_NULL_HANDLE;
    VkImageCreateInfo m_desc = {};
	VkClearValue      m_clearValue = {};

	CreationInfo  m_creationInfo = {}; // for resize
	ResourceState m_currentState = {};

	bool m_bOwnedBySwapChain = false;
};

} // namespace vk