#pragma once
#include "VkBarrierState.h"

namespace vk
{

struct Subresource
{
	Subresource() : Subresource(0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS) {}
	Subresource(u32 baseMip, u32 mipLevels, u32 baseLayer, u32 layerCount)
		: baseMip(baseMip), mipLevels(mipLevels), baseLayer(baseLayer), arrayLayers(layerCount) {
	}
	Subresource(VkImageSubresourceRange range)
		: Subresource(range.baseMipLevel, range.levelCount, range.baseArrayLayer, range.layerCount) {
	}
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

struct ResourceState
{
	ResourceState() = default;
	explicit ResourceState(const BarrierState& state_)
		: state(state_) {}
	ResourceState(VkAccessFlags2 access, VkPipelineStageFlags2 stage, VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED)
		: state(access, stage, layout) {}

	// iterator (subresource map)
	auto begin() const noexcept { return subresourceStates.begin(); }
	auto end()   const noexcept { return subresourceStates.end(); }

	[[nodiscard]]
	bool IsValid() const
	{
		return state.access != 0 || state.stage != 0 || state.layout != VK_IMAGE_LAYOUT_UNDEFINED;
	}

	void SetSubresourceState(const BarrierState& state_, const Subresource& subresource)
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

	// Convenience: set whole-resource state (for buffers)
	void SetState(const BarrierState& state_)
	{
		state = state_;
		subresourceStates.clear();
	}

	[[nodiscard]]
	const BarrierState& GetSubresourceState(const Subresource& subresource = VK_ALL_SUBRESOURCES) const
	{
		if (!(subresource == VK_ALL_SUBRESOURCES))
		{
			const auto iter = subresourceStates.find(subresource);
			if (iter != subresourceStates.end())
				return iter->second;
		}
		return state;
	}

	[[nodiscard]]
	bool HasDivergentSubresources() const { return !subresourceStates.empty(); }

	BarrierState                          state = {};
	std::map< Subresource, BarrierState > subresourceStates;
};

template< typename TResource >
class VulkanResource
{
public:
	VulkanResource(VkRenderDevice& rd, const char* name)
		: m_RenderDevice(rd), m_Name(name) {}
	virtual ~VulkanResource() = default;

	void SetState(const BarrierState& state, Subresource subresource = VK_ALL_SUBRESOURCES) { m_CurrentState.SetSubresourceState(state, subresource); }

	[[nodiscard]]
	inline const ResourceState& GetState() const { return m_CurrentState; }

protected:
	void SetDeviceObjectName(u64 handle, VkObjectType type)
	{
		m_RenderDevice.SetVkObjectName(m_Name, handle, type);
	}

	VkRenderDevice& m_RenderDevice;
	std::string     m_Name;

	VmaAllocation     m_vmaAllocation  = VK_NULL_HANDLE;
	VmaAllocationInfo m_AllocationInfo = {};

	ResourceState m_CurrentState = {};
};

}