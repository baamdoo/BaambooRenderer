#include "RendererPch.h"
#include "Dx12Sampler.h"

namespace dx12
{

Arc< Sampler > Sampler::Create(RenderDevice& device, const std::wstring& name, CreationInfo&& info)
{
    return MakeArc< Sampler >(device, name, std::move(info));
}

Arc< Sampler > Sampler::CreateLinearRepeat(RenderDevice& device, const std::wstring& name) {
    return Create(device, name,
        {
            .filter      = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            .addressMode = D3D12_TEXTURE_ADDRESS_MODE_WRAP
        });
}

Arc< Sampler > Sampler::CreateLinearClamp(RenderDevice& device, const std::wstring& name)
{
    return Create(device, name,
        {
            .filter      = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            .addressMode = D3D12_TEXTURE_ADDRESS_MODE_CLAMP
        });
}

Arc< Sampler > Sampler::CreatePointRepeat(RenderDevice& device, const std::wstring& name)
{
    return Create(device, name,
        {
            .filter        = D3D12_FILTER_MIN_MAG_MIP_POINT,
            .addressMode   = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .maxAnisotropy = 0
        });
}

Arc< Sampler > Sampler::CreatePointClamp(RenderDevice& device, const std::wstring& name)
{
    return Create(device, name,
        {
            .filter        = D3D12_FILTER_MIN_MAG_MIP_POINT,
            .addressMode   = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            .maxAnisotropy = 0
        });
}

Arc< Sampler > Sampler::CreateLinearClampCmp(RenderDevice& device, const std::wstring& name)
{
    return Create(device, name,
        {
            .filter        = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
            .addressMode   = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            .maxAnisotropy = 0,
            .compareOp     = D3D12_COMPARISON_FUNC_LESS_EQUAL,
            .borderColor   = { 1.0f, 1.0f, 1.0f, 1.0f }
        });
}

Sampler::Sampler(RenderDevice& device, const std::wstring& name, CreationInfo&& info)
	: Super(device, name, eResourceType::Sampler)
{
	D3D12_SAMPLER_DESC desc = {};
	desc.Filter             = info.filter;
	desc.MipLODBias         = info.mipLodBias;
	desc.MaxAnisotropy      = info.maxAnisotropy;
	desc.ComparisonFunc     = info.compareOp;
	desc.MinLOD             = info.minLod;
	desc.MaxLOD             = info.maxLod;
	desc.BorderColor[0]     = info.borderColor[0];
	desc.BorderColor[1]     = info.borderColor[1];
	desc.BorderColor[2]     = info.borderColor[2];
	desc.BorderColor[3]     = info.borderColor[3];
    desc.AddressU = desc.AddressV = desc.AddressW = info.addressMode;

	auto d3d12Device = m_RenderDevice.GetD3D12Device();
	d3d12Device->CreateSampler(&desc, m_SamplerView);
}

Sampler::~Sampler()
{
}

} // namespace dx12
