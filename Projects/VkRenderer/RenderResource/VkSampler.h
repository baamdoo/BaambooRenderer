#pragma once
#include "VkResource.h"

namespace vk
{

enum eSamplerInterpolation
{
	Linear,
    Nearest,
    Cubic,
};

enum class eSamplerType
{
    Repeat,
    Clamp,
    Mirrored,
};

class Sampler : public Resource< Sampler >
{
using Super = Resource< Sampler >;

public:
	struct CreationInfo
	{
        eSamplerType          type;
        eSamplerInterpolation interpolation;
        f32                   mipLodBias;
        f32                   maxAnisotropy;
        VkCompareOp           compareOp;
        f32                   lod; // assume always minLOD = 0
        VkBorderColor         borderColor;
	};

	[[nodiscard]]
	inline VkSampler vkSampler() const { return m_vkSampler; }

protected:
    template< typename T >
    friend class ResourcePool;

    Sampler(RenderContext& context, std::wstring_view name, const CreationInfo& info);
    virtual ~Sampler();

private:
	VkSampler m_vkSampler = VK_NULL_HANDLE;
};

} // namespace vk