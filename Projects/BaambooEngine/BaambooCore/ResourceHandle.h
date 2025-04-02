#pragma once

namespace baamboo
{

constexpr u8  MAX_FRAME_INDEX = 8u;
constexpr u16 INVALID_VERSION = UINT8_MAX;
constexpr u32 INVALID_INDEX = (~0u) / ((INVALID_VERSION + 1) * MAX_FRAME_INDEX * 2);

template< typename TResource >
class ResourceHandle
{
public:
	ResourceHandle() = default;
	ResourceHandle(u32 idx, u8 ver) :
		index(idx), version(ver) {}
	bool operator==(const ResourceHandle< TResource >& other) const { return index == other.index && version == other.version; }
	bool operator!=(const ResourceHandle< TResource >& other) const { return !(this == other); }

	[[nodiscard]]
	u32 Index() const { return index; }
	[[nodiscard]]
	u32 Version() const { return version; }
	[[nodiscard]]
	bool IsValid() const { return index != INVALID_INDEX && version != INVALID_VERSION; }

private:
	u32 state   : 1 = 0;
	u32 frame   : 3 = 0;
	u32 version : 8 = INVALID_VERSION;
	u32 index   : 20 = INVALID_INDEX;
};

} // namespace baamboo