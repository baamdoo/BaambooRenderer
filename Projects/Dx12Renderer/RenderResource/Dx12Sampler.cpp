#include "RendererPch.h"
#include "Dx12Sampler.h"

namespace dx12
{

using namespace render;

#define DX12_SAMPLER_FILTER(filter, mode) ConvertToDx12SamplerMipMapFilter(filter, mode)
D3D12_FILTER ConvertToDx12SamplerMipMapFilter(eFilterMode filter, eMipmapMode mode)
{
    switch (filter)
    {
    case eFilterMode::Point:
        return D3D12_FILTER_MIN_MAG_MIP_POINT;
    case eFilterMode::Linear:
        return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    case eFilterMode::Anisotropic:
        return D3D12_FILTER_COMPARISON_ANISOTROPIC;

    default:
        assert(false && "Invalid filter mode!"); break;
    }
    
    return D3D12_FILTER_MIN_MAG_MIP_POINT;
}

#define DX12_SAMPLER_ADDRESS(mode) ConvertToDx12SamplerAddressMode(mode)
D3D12_TEXTURE_ADDRESS_MODE ConvertToDx12SamplerAddressMode(eAddressMode mode)
{
    switch (mode)
    {
    case eAddressMode::Wrap:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case eAddressMode::MirrorRepeat:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case eAddressMode::ClampEdge:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case eAddressMode::ClampBorder:
        return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case eAddressMode::MirrorClampEdge:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;

    default:
        assert(false && "Invalid address mode!"); break;
    }

    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

#define DX12_SAMPLER_COMPAREOP(op) ConvertToDx12SamplerCompareOp(op)
D3D12_COMPARISON_FUNC ConvertToDx12SamplerCompareOp(eCompareOp op)
{
    switch (op)
    {
    case eCompareOp::Never        : return D3D12_COMPARISON_FUNC_NEVER;
    case eCompareOp::Less         : return D3D12_COMPARISON_FUNC_LESS;
    case eCompareOp::Equal        : return D3D12_COMPARISON_FUNC_EQUAL;
    case eCompareOp::LessEqual    : return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case eCompareOp::Greater      : return D3D12_COMPARISON_FUNC_GREATER;
    case eCompareOp::NotEqual     : return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case eCompareOp::GreaterEqual : return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case eCompareOp::Always       : return D3D12_COMPARISON_FUNC_ALWAYS;

    default:
        assert(false && "Invalid compare op!"); break;
    }

    return D3D12_COMPARISON_FUNC_NONE;
}

#define DX12_SAMPLER_BORDERCOLOR(color) ConvertToDx12SamplerBorderColor(color)
float ConvertToDx12SamplerBorderColor(eBorderColor color)
{
    switch (color)
    {
    case eBorderColor::TransparentBlack_Float : return 0.0f;
    case eBorderColor::OpaqueBlack_Float      : return 1.0f;
    case eBorderColor::OpaqueWhite_Float      : return 1.0f;

    default:
        assert(false && "Invalid boder color!"); break;
    }

    return 0.0f;
}


Arc< Dx12Sampler > Dx12Sampler::Create(Dx12RenderDevice& rd, const std::string& name, CreationInfo&& info)
{
    return MakeArc< Dx12Sampler >(rd, name, std::move(info));
}

Arc< Dx12Sampler > Dx12Sampler::CreateLinearRepeat(Dx12RenderDevice& rd, const std::string& name) {
    return Create(rd, name,
        {
            .filter      = eFilterMode::Linear,
            .addressMode = eAddressMode::Wrap
        });
}

Arc< Dx12Sampler > Dx12Sampler::CreateLinearClamp(Dx12RenderDevice& rd, const std::string& name)
{
    return Create(rd, name,
        {
            .filter      = eFilterMode::Linear,
            .addressMode = eAddressMode::ClampEdge
        });
}

Arc< Dx12Sampler > Dx12Sampler::CreatePointRepeat(Dx12RenderDevice& rd, const std::string& name)
{
    return Create(rd, name,
        {
            .filter        = eFilterMode::Point,
            .addressMode   = eAddressMode::Wrap,
            .maxAnisotropy = 0.0f
        });
}

Arc< Dx12Sampler > Dx12Sampler::CreatePointClamp(Dx12RenderDevice& rd, const std::string& name)
{
    return Create(rd, name,
        {
            .filter        = eFilterMode::Point,
            .addressMode   = eAddressMode::ClampEdge,
            .maxAnisotropy = 0.0f
        });
}

Arc< Dx12Sampler > Dx12Sampler::CreateLinearClampCmp(Dx12RenderDevice& rd, const std::string& name)
{
    return Create(rd, name,
        {
            .filter        = eFilterMode::Linear,
            .addressMode   = eAddressMode::ClampBorder,
            .maxAnisotropy = 0.0f,
            .compareOp     = eCompareOp::LessEqual,
            .borderColor   = eBorderColor::OpaqueWhite_Float
        });
}

Dx12Sampler::Dx12Sampler(Dx12RenderDevice& rd, const std::string& name, CreationInfo&& info)
	: render::Sampler(name, std::move(info))
    , Dx12Resource(rd, name, eResourceType::Sampler)
{
	D3D12_SAMPLER_DESC desc = {};
	desc.Filter         = DX12_SAMPLER_FILTER(m_CreationInfo.filter, m_CreationInfo.mipmapMode);
	desc.MipLODBias     = m_CreationInfo.mipLodBias;
	desc.MaxAnisotropy  = m_CreationInfo.maxAnisotropy;
	desc.ComparisonFunc = DX12_SAMPLER_COMPAREOP(m_CreationInfo.compareOp);
	desc.MinLOD         = m_CreationInfo.minLod;
	desc.MaxLOD         = m_CreationInfo.maxLod;
	desc.BorderColor[0] = 
	desc.BorderColor[1] = 
	desc.BorderColor[2] = 
	desc.BorderColor[3] = DX12_SAMPLER_BORDERCOLOR(m_CreationInfo.borderColor);
    desc.AddressU = desc.AddressV = desc.AddressW = DX12_SAMPLER_ADDRESS(m_CreationInfo.addressMode);

    // currently only static sampler is utilized in D3D12
	//auto d3d12Device = m_RenderDevice.GetD3D12Device();
	//d3d12Device->CreateSampler(&desc, m_SamplerView);
}

Dx12Sampler::~Dx12Sampler()
{
}

} // namespace dx12
