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

inline u32 CalculateMipCount(u32 width, u32 height, u32 depth)
{
	return (u32)std::floor(std::log2(glm::min(glm::min(width, height), depth))) + 1;
}

inline float3 SmoothStep(const float3& v1, const float3& v2, float t)
{
	t = (t > 1.0f) ? 1.0f : ((t < 0.0f) ? 0.0f : t);
	t = t * t * (3.0f - 2.0f * t);

	return glm::mix(v1, v2, t);
}


static constexpr int MAX_HALTON_SEQUENCE = 16;
static constexpr glm::vec2 HALTON_SEQUENCE[MAX_HALTON_SEQUENCE] = 
{
	glm::vec2(0.5, 0.333333),
	glm::vec2(0.25, 0.666667),
	glm::vec2(0.75, 0.111111),
	glm::vec2(0.125, 0.444444),
	glm::vec2(0.625, 0.777778),
	glm::vec2(0.375, 0.222222),
	glm::vec2(0.875, 0.555556) ,
	glm::vec2(0.0625, 0.888889),
	glm::vec2(0.5625, 0.037037),
	glm::vec2(0.3125, 0.37037),
	glm::vec2(0.8125, 0.703704),
	glm::vec2(0.1875, 0.148148),
	glm::vec2(0.6875, 0.481482),
	glm::vec2(0.4375, 0.814815),
	glm::vec2(0.9375, 0.259259),
	glm::vec2(0.03125, 0.592593)
};

inline glm::vec2 GetHaltonSequence(uint32_t idx)
{
	return HALTON_SEQUENCE[idx % MAX_HALTON_SEQUENCE];
}

}

} // namespace baamboo