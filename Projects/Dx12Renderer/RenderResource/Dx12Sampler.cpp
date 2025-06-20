#include "RendererPch.h"
#include "Dx12Sampler.h"

namespace dx12
{

Sampler::Sampler(RenderDevice& device, const std::wstring& name, CreationInfo&& info)
	: Super(device, name, eResourceType::Sampler)
{
	D3D12_SAMPLER_DESC desc = {};
	desc.Filter = info.filter;
	desc.AddressU = desc.AddressV = desc.AddressW = info.type == eSamplerType::Mirrored ?
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR : info.type == eSamplerType::Repeat ?
		D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	desc.MipLODBias = info.mipLodBias;
	desc.MaxAnisotropy = info.maxAnisotropy;
	desc.ComparisonFunc = info.compareOp;
	desc.BorderColor[0] = info.borderColor[0];
	desc.BorderColor[1] = info.borderColor[1];
	desc.BorderColor[2] = info.borderColor[2];
	desc.BorderColor[3] = info.borderColor[3];
	desc.MinLOD = 0.0f;
	desc.MaxLOD = info.lod;

	auto d3d12Device = m_RenderDevice.GetD3D12Device();
	d3d12Device->CreateSampler(&desc, m_SamplerView);
}

Sampler::~Sampler()
{
}

} // namespace dx12
