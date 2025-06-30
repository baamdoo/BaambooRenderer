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
        D3D12_FILTER               filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        D3D12_TEXTURE_ADDRESS_MODE addressMode    = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        f32                        mipLodBias     = D3D12_DEFAULT_MIP_LOD_BIAS;
        u32                        maxAnisotropy  = 16;
        D3D12_COMPARISON_FUNC      compareOp      = D3D12_COMPARISON_FUNC_NEVER;
        f32                        minLod         = 0.0f;
        f32                        maxLod         = D3D12_FLOAT32_MAX;
        f32                        borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    static Arc< Sampler > Create(RenderDevice& device, const std::wstring& name, CreationInfo&& info);

    static Arc< Sampler > CreateLinearRepeat(RenderDevice& device, const std::wstring& name = L"LinearRepeat");
    static Arc< Sampler > CreateLinearClamp(RenderDevice& device, const std::wstring& name = L"LinearClamp");
    static Arc< Sampler > CreatePointRepeat(RenderDevice& device, const std::wstring& name = L"PointRepeat");
    static Arc< Sampler > CreatePointClamp(RenderDevice& device, const std::wstring& name = L"PointClamp");
    static Arc< Sampler > CreateLinearClampCmp(RenderDevice& device, const std::wstring& name = L"Shadow");

    Sampler(RenderDevice& device, const std::wstring& name, CreationInfo&& info);
    virtual ~Sampler();

private:
    D3D12_CPU_DESCRIPTOR_HANDLE m_SamplerView = {};
};

} // namespace dx12