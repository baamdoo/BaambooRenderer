#include "RendererPch.h"
#include "Dx12RootSignature.h"

namespace dx12
{
	
namespace StaticSamplerPresets
{
	// Point filtering with texture coordinate wrapping.
	// Ideal for tiling pixel art or non-interpolated patterns.
    constexpr D3D12_STATIC_SAMPLER_DESC PointWrap
    {
        .Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT,
        .AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .MipLODBias       = 0.0f,
        .MaxAnisotropy    = 1,
        .ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER,
        .BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        .MinLOD           = 0.0f,
        .MaxLOD           = D3D12_FLOAT32_MAX,
        .ShaderRegister   = static_cast<u32>(render::eSamplerIndex::PointWrap),
        .RegisterSpace    = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };

	// Point filtering with texture coordinate clamping.
	// Ideal for non-tiling UI elements or sprites.
    constexpr D3D12_STATIC_SAMPLER_DESC PointClamp
    {
        .Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT,
        .AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .MipLODBias       = 0.0f,
        .MaxAnisotropy    = 1,
        .ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER,
        .BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        .MinLOD           = 0.0f,
        .MaxLOD           = D3D12_FLOAT32_MAX,
        .ShaderRegister   = static_cast<u32>(render::eSamplerIndex::PointClamp),
        .RegisterSpace    = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };

	// Bilinear filtering with texture coordinate wrapping.
	// Common for tiling textures without mipmaps.
    constexpr D3D12_STATIC_SAMPLER_DESC LinearWrap
    {
        .Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
        .AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .MipLODBias       = 0.0f,
        .MaxAnisotropy    = 1,
        .ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER,
        .BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        .MinLOD           = 0.0f,
        .MaxLOD           = D3D12_FLOAT32_MAX,
        .ShaderRegister   = static_cast<u32>(render::eSamplerIndex::LinearWrap),
        .RegisterSpace    = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };

    // Bilinear filtering with texture coordinate clamping.
    // Common for non-tiling textures without mipmaps.
    constexpr D3D12_STATIC_SAMPLER_DESC LinearClamp
    {
        .Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, // Bilinear
        .AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .MipLODBias       = 0.0f,
        .MaxAnisotropy    = 1,
        .ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER,
        .BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        .MinLOD           = 0.0f,
        .MaxLOD           = D3D12_FLOAT32_MAX,
        .ShaderRegister   = static_cast<u32>(render::eSamplerIndex::LinearClamp),
        .RegisterSpace    = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };

    // Trilinear filtering with texture coordinate wrapping.
    // The standard for most high-quality tiling world-space textures.
    constexpr D3D12_STATIC_SAMPLER_DESC TrilinearWrap
    {
        .Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        .AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .MipLODBias       = 0.0f,
        .MaxAnisotropy    = 1,
        .ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER,
        .BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        .MinLOD           = 0.0f,
        .MaxLOD           = D3D12_FLOAT32_MAX,
        .ShaderRegister   = static_cast<u32>(render::eSamplerIndex::TrilinearWrap),
        .RegisterSpace    = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };

    // Anisotropic filtering (16x) with texture coordinate wrapping.
    // Highest quality for textures viewed at sharp angles (e.g., ground, floors).
    constexpr D3D12_STATIC_SAMPLER_DESC AnisotropicWrap
    {
        .Filter           = D3D12_FILTER_ANISOTROPIC,
        .AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .MipLODBias       = 0.0f,
        .MaxAnisotropy    = 16,
        .ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER,
        .BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        .MinLOD           = 0.0f,
        .MaxLOD           = D3D12_FLOAT32_MAX,
        .ShaderRegister   = static_cast<u32>(render::eSamplerIndex::AnisotropicWrap),
        .RegisterSpace    = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };

    // Comparison sampler for Percentage-Closer Filtering (PCF).
    // Uses border addressing to return 1.0 (not in shadow) for samples outside the map.
    constexpr D3D12_STATIC_SAMPLER_DESC ShadowCmpLessEqual
    {
        .Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        .AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        .AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        .AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        .MipLODBias       = 0.0f,
        .MaxAnisotropy    = 1,
        .ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL,
        .BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE, // Corresponds to 1.0f depth
        .MinLOD           = 0.0f,
        .MaxLOD           = D3D12_FLOAT32_MAX,
        .ShaderRegister   = static_cast<u32>(render::eSamplerIndex::ShadowCmpLessEqual),
        .RegisterSpace    = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };

    // Point-clamp with MIN reduction for HiZ occlusion culling.
    // Returns the minimum sampled value at the requested mip level.
    constexpr D3D12_STATIC_SAMPLER_DESC PointClampMin
    {
        .Filter           = D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT,
        .AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .MipLODBias       = 0.0f,
        .MaxAnisotropy    = 1,
        .ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER,
        .BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        .MinLOD           = 0.0f,
        .MaxLOD           = D3D12_FLOAT32_MAX,
        .ShaderRegister   = static_cast<u32>(render::eSamplerIndex::PointClampMin),
        .RegisterSpace    = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };

    // Linear-clamp with MIN reduction for HiZ occlusion testing.
    // SampleLevel returns min of the 2x2 bilinear footprint at the requested
    // mip level, handling texel-boundary straddling without extra samples.
    constexpr D3D12_STATIC_SAMPLER_DESC LinearClampMin
    {
        .Filter           = D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR,
        .AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .MipLODBias       = 0.0f,
        .MaxAnisotropy    = 1,
        .ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER,
        .BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        .MinLOD           = 0.0f,
        .MaxLOD           = D3D12_FLOAT32_MAX,
        .ShaderRegister   = static_cast<u32>(render::eSamplerIndex::LinearClampMin),
        .RegisterSpace    = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };
}
static D3D12_STATIC_SAMPLER_DESC s_StaticSamplerPresets[static_cast<u32>(render::eSamplerIndex::MaxIndex)];

Dx12RootSignature::Dx12RootSignature(Dx12RenderDevice& rd, const std::string& name, D3D12_ROOT_SIGNATURE_FLAGS flags)
	: m_RenderDevice(rd)
	, m_Name(name)
	, m_Flags(flags)
{
	s_StaticSamplerPresets[static_cast<u32>(render::eSamplerIndex::PointWrap)]          = StaticSamplerPresets::PointWrap;
	s_StaticSamplerPresets[static_cast<u32>(render::eSamplerIndex::PointClamp)]         = StaticSamplerPresets::PointClamp;
	s_StaticSamplerPresets[static_cast<u32>(render::eSamplerIndex::LinearWrap)]         = StaticSamplerPresets::LinearWrap;
	s_StaticSamplerPresets[static_cast<u32>(render::eSamplerIndex::LinearClamp)]        = StaticSamplerPresets::LinearClamp;
	s_StaticSamplerPresets[static_cast<u32>(render::eSamplerIndex::TrilinearWrap)]      = StaticSamplerPresets::TrilinearWrap;
	s_StaticSamplerPresets[static_cast<u32>(render::eSamplerIndex::AnisotropicWrap)]    = StaticSamplerPresets::AnisotropicWrap;
	s_StaticSamplerPresets[static_cast<u32>(render::eSamplerIndex::ShadowCmpLessEqual)] = StaticSamplerPresets::ShadowCmpLessEqual;
	s_StaticSamplerPresets[static_cast<u32>(render::eSamplerIndex::PointClampMin)]      = StaticSamplerPresets::PointClampMin;
	s_StaticSamplerPresets[static_cast<u32>(render::eSamplerIndex::LinearClampMin)]     = StaticSamplerPresets::LinearClampMin;
}

Dx12RootSignature::~Dx12RootSignature()
{
	COM_RELEASE(m_d3d12RootSignature);
}

u32 Dx12RootSignature::AddConstants(u32 reg, u32 space, u32 numConstants, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1& param = m_RootParameters.emplace_back();
	param.InitAsConstants(numConstants, reg, space, visibility);

	const u32 rootIndex = static_cast<u32>(m_RootParameters.size()) - 1;
	m_RootIndexMap.emplace(RootKey{ D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, MAKELONG(space, reg) }, rootIndex);
	return rootIndex;
}

u32 Dx12RootSignature::AddSamplerPreset(u32 preset)
{
	m_StaticSamplers.emplace_back(CD3DX12_STATIC_SAMPLER_DESC(s_StaticSamplerPresets[preset]));
	return static_cast<u32>(m_StaticSamplers.size()) - 1;
}

void Dx12RootSignature::Build()
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.Init_1_1(
		static_cast<u32>(m_RootParameters.size()),
		m_RootParameters.data(), 
		static_cast<u32>(m_StaticSamplers.size()), 
		m_StaticSamplers.data(), 
		m_Flags
	);

	D3D_ROOT_SIGNATURE_VERSION highestVersion = m_RenderDevice.GetHighestRootSignatureVersion();
	ID3DBlob* d3dSignature = nullptr;
	ID3DBlob* d3dError = nullptr;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, highestVersion, &d3dSignature, &d3dError);
	if (FAILED(hr))
	{
		if (d3dError != nullptr)
		{
			const char* errorMsg = static_cast<const char*>(d3dError->GetBufferPointer());

			OutputDebugStringA("Root Signature Serialization Error:\n");
			OutputDebugStringA(errorMsg);

			d3dError->Release();
		}

		__debugbreak();
	}
	ThrowIfFailed(d3d12Device->CreateRootSignature(
		0, d3dSignature->GetBufferPointer(), d3dSignature->GetBufferSize(), IID_PPV_ARGS(&m_d3d12RootSignature))
	);
	m_d3d12RootSignature->SetName(ConvertToWString(m_Name).c_str());

	COM_RELEASE(d3dSignature);
	COM_RELEASE(d3dError);
}

u32 Dx12RootSignature::GetRootIndex(D3D12_ROOT_PARAMETER_TYPE type, u32 space, u32 reg) const
{
	const auto& it = m_RootIndexMap.find({ type, MAKELONG(space, reg) });
	if (it == m_RootIndexMap.end())
	{
		return kInvalidIndex;
	}

	return it->second;
}

}