#pragma once
#include "Dx12Resource.h"

namespace dx12
{

enum class eSamplerType
{
    Repeat,
    Clamp,
    Mirrored,
};

class Sampler : public Resource
{
using Super = Resource;

public:
    struct CreationInfo
    {
        eSamplerType          type = eSamplerType::Repeat;
        D3D12_FILTER          filter;
        f32                   mipLodBias;
        u32                   maxAnisotropy;
        D3D12_COMPARISON_FUNC compareOp;
        f32                   lod; // assume always minLOD = 0
        f32                   borderColor[4];
    };

protected:
    template< typename T >
    friend class ResourcePool;
    friend class ResourceManager;

	Sampler(RenderContext& context, std::wstring_view name, CreationInfo&& info);
    virtual ~Sampler();

private:
    D3D12_CPU_DESCRIPTOR_HANDLE m_SamplerView = {};
};

} // namespace dx12