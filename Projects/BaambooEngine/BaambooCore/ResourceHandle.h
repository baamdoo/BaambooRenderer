#pragma once

namespace baamboo
{

constexpr u32 VERSION_BIT = 8;
constexpr u32 INDEX_BIT = 24;
constexpr u16 INVALID_VERSION = UINT8_MAX;
constexpr u32 INVALID_INDEX = (~0u) / ((INVALID_VERSION + 1));

template< typename TResource >
class ResourceHandle
{
public:
	ResourceHandle() = default;
	ResourceHandle(u32 idx, u8 ver) :
		index(idx), version(ver) {}
	ResourceHandle(u32 id) :
		index(id & INVALID_INDEX), version((id >> INDEX_BIT) & INVALID_VERSION) {}
	bool operator==(const ResourceHandle< TResource >& other) const { return index == other.index && version == other.version; }
	bool operator!=(const ResourceHandle< TResource >& other) const { return !(*this == other); }
	explicit operator u32() const { return (version << 24) | (index & INVALID_INDEX); }

	[[nodiscard]]
	u32 Index() const { return index; }
	[[nodiscard]]
	u32 Version() const { return version; }
	[[nodiscard]]
	bool IsValid() const { return index != INVALID_INDEX && version != INVALID_VERSION; }

	void Reset() { index = INVALID_INDEX; version = INVALID_VERSION; }

private:
	u32 version : VERSION_BIT = INVALID_VERSION;
	u32 index   : INDEX_BIT = INVALID_INDEX;
};

} // namespace baamboo
