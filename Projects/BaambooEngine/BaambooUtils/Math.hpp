#pragma once

namespace baamboo
{

namespace math
{

//-------------------------------------------------------------------------
// Helper Function
//-------------------------------------------------------------------------
template< typename T >
inline T AlignUp(T size, T alignment)
{
	return (size + alignment - 1) & ~(alignment - 1);
}

inline u32 CalculateMipCount(u32 width, u32 height)
{
	return (u32)std::floor(std::log2(glm::min(width, height))) + 1;
}

}

} // namespace baamboo