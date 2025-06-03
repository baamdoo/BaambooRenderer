#pragma once
#include <glm/glm.hpp>

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

inline float3 SmoothStep(const float3& v1, const float3& v2, float t)
{
	t = (t > 1.0f) ? 1.0f : ((t < 0.0f) ? 0.0f : t);
	t = t * t * (3.0f - 2.0f * t);

	return glm::mix(v1, v2, t);
}

}

} // namespace baamboo