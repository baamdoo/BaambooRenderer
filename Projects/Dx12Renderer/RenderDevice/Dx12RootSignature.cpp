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
        .ShaderRegister   = static_cast<u32>(eSamplerIndex::PointWrap),
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
        .ShaderRegister   = static_cast<u32>(eSamplerIndex::PointClamp),
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
        .ShaderRegister   = static_cast<u32>(eSamplerIndex::LinearWrap),
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
        .ShaderRegister   = static_cast<u32>(eSamplerIndex::LinearClamp),
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
        .ShaderRegister   = static_cast<u32>(eSamplerIndex::TrilinearWrap),
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
        .ShaderRegister   = static_cast<u32>(eSamplerIndex::AnisotropicWrap),
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
        .ShaderRegister   = static_cast<u32>(eSamplerIndex::ShadowCmpLessEqual),
        .RegisterSpace    = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };
}
static D3D12_STATIC_SAMPLER_DESC s_StaticSamplerPresets[static_cast<u32>(eSamplerIndex::MaxIndex)];

Dx12RootSignature::Dx12RootSignature(Dx12RenderDevice& rd, const std::string& name, D3D12_ROOT_SIGNATURE_FLAGS flags)
	: m_RenderDevice(rd)
	, m_Name(name)
	, m_Flags(flags)
{
	//InitCommonDescriptors();

	s_StaticSamplerPresets[static_cast<u32>(eSamplerIndex::PointWrap)]          = StaticSamplerPresets::PointWrap;
	s_StaticSamplerPresets[static_cast<u32>(eSamplerIndex::PointClamp)]         = StaticSamplerPresets::PointClamp;
	s_StaticSamplerPresets[static_cast<u32>(eSamplerIndex::LinearWrap)]         = StaticSamplerPresets::LinearWrap;
	s_StaticSamplerPresets[static_cast<u32>(eSamplerIndex::LinearClamp)]        = StaticSamplerPresets::LinearClamp;
	s_StaticSamplerPresets[static_cast<u32>(eSamplerIndex::TrilinearWrap)]      = StaticSamplerPresets::TrilinearWrap;
	s_StaticSamplerPresets[static_cast<u32>(eSamplerIndex::AnisotropicWrap)]    = StaticSamplerPresets::AnisotropicWrap;
	s_StaticSamplerPresets[static_cast<u32>(eSamplerIndex::ShadowCmpLessEqual)] = StaticSamplerPresets::ShadowCmpLessEqual;
}

Dx12RootSignature::~Dx12RootSignature()
{
	COM_RELEASE(m_d3d12RootSignature);

	m_SamplerTableBitMask = 0;
	m_DescriptorTableBitMask = 0;
}

u32 Dx12RootSignature::AddConstants(u32 reg, u32 numConstants, D3D12_SHADER_VISIBILITY visibility)
{
	return AddConstants(reg, ROOT_CONSTANT_SPACE, numConstants, visibility);
}

u32 Dx12RootSignature::AddConstants(u32 reg, u32 space, u32 numConstants, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1 param = {};
	param.InitAsConstants(numConstants, reg, space, visibility);
	return AddParameter(param);
}

u32 Dx12RootSignature::AddCBV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1 param = {};
	param.InitAsConstantBufferView(reg, space, flags, visibility);
	return AddParameter(param);
}

u32 Dx12RootSignature::AddSRV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1 param = {};
	param.InitAsShaderResourceView(reg, space, flags, visibility);
	return AddParameter(param);
}

u32 Dx12RootSignature::AddUAV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1 param = {};
	param.InitAsUnorderedAccessView(reg, space, flags, visibility);
	return AddParameter(param);
}

u32 Dx12RootSignature::AddSampler(
	UINT                       shaderRegister, 
	UINT                       registerSpace, 
	D3D12_FILTER               filter, 
	D3D12_TEXTURE_ADDRESS_MODE addressUVW, 
	UINT                       maxAnisotropy, 
	D3D12_COMPARISON_FUNC      comparisonFunc, 
	D3D12_STATIC_BORDER_COLOR  borderColor)
{
	CD3DX12_STATIC_SAMPLER_DESC& desc = m_StaticSamplers.emplace_back();
	desc.Init(shaderRegister, filter, addressUVW, addressUVW, addressUVW, 0.0f, maxAnisotropy, comparisonFunc, borderColor);
	desc.RegisterSpace = registerSpace;

	return static_cast<u32>(m_StaticSamplers.size()) - 1;
}

u32 Dx12RootSignature::AddSamplerPreset(u32 preset)
{
	m_StaticSamplers.emplace_back(CD3DX12_STATIC_SAMPLER_DESC(s_StaticSamplerPresets[preset]));
	return static_cast<u32>(m_StaticSamplers.size()) - 1;
}

u32 Dx12RootSignature::AddDescriptorTable(const DescriptorTable& table, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1 param = {};
	param.InitAsDescriptorTable(static_cast<u32>(table.Size()), nullptr, visibility);
	m_DescriptorTableIndices.push_back(static_cast<u32>(m_DescriptorTables.size()));
	m_DescriptorTables.push_back(table);
	return AddParameter(param);
}

void Dx12RootSignature::Build()
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	u32 t = 0;
	for (size_t i = 0; i < m_RootParameters.size(); ++i)
	{
		if (m_RootParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			m_RootParameters[i].DescriptorTable.NumDescriptorRanges = (u32)m_DescriptorTables[m_DescriptorTableIndices[t]].Size();
			m_RootParameters[i].DescriptorTable.pDescriptorRanges = m_DescriptorTables[m_DescriptorTableIndices[t]].Data();
			t++;
		}
	}

	u32 numParameters = static_cast<u32>(m_RootParameters.size());
	for (u32 i = 0; i < numParameters; ++i)
	{
		auto& rootParam = m_RootParameters[i];
		if (rootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			if (rootParam.DescriptorTable.NumDescriptorRanges > 0)
			{
				switch (rootParam.DescriptorTable.pDescriptorRanges->RangeType)
				{
				case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
				case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
				case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
					m_DescriptorTableBitMask |= (1LL << i);
					break;
				case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
					m_SamplerTableBitMask |= (1LL << i);
					break;
				}
			}

			for (u32 j = 0; j < rootParam.DescriptorTable.NumDescriptorRanges; ++j)
			{
				m_NumDescriptorsPerTable[i] += rootParam.DescriptorTable.pDescriptorRanges[j].NumDescriptors;
			}
		}
	}

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.Init_1_1(
		numParameters, 
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

void Dx12RootSignature::UpdateVisibility(u32 rootIndex, D3D12_SHADER_VISIBILITY visibility)
{
	assert(rootIndex < m_RootParameters.size());
	m_RootParameters[rootIndex].ShaderVisibility = visibility;
}

void Dx12RootSignature::CopySignatureParams(Dx12RootSignature& srcSignature)
{
	m_RootParameters = srcSignature.m_RootParameters;
	m_StaticSamplers = srcSignature.m_StaticSamplers;
}

u32 Dx12RootSignature::GetNumDescriptors(u32 rootIndex) const
{
	assert(rootIndex < MAX_ROOT_INDEX);
	return m_NumDescriptorsPerTable[rootIndex];
}

u64 Dx12RootSignature::GetDescriptorTableBitMask(D3D12_DESCRIPTOR_HEAP_TYPE type) const
{
	u64 mask = 0;
	switch (type)
	{
	case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
		mask = m_DescriptorTableBitMask;
		break;
	case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
		mask = m_SamplerTableBitMask;
		break;
	}

	return mask;
}

u32 Dx12RootSignature::GetRootIndex(u32 space, u32 reg) const
{
	const auto& it = m_RootIndexMap.find(MAKELONG(space, reg));
	if (it == m_RootIndexMap.end())
	{
		return INVALID_INDEX;
	}

	return it->second;
}

u32 Dx12RootSignature::AddParameter(const CD3DX12_ROOT_PARAMETER1& param)
{
	m_RootParameters.emplace_back(param);
	u32 rootIndex = static_cast<u32>(m_RootParameters.size()) - 1;

	u32 reg   = INVALID_INDEX;
	u32 space = INVALID_INDEX;
	switch (param.ParameterType)
	{
	case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
		reg   = param.Constants.ShaderRegister;
		space = param.Constants.RegisterSpace;
		break;
	case D3D12_ROOT_PARAMETER_TYPE_CBV:
	case D3D12_ROOT_PARAMETER_TYPE_SRV:
	case D3D12_ROOT_PARAMETER_TYPE_UAV:
		reg   = param.Descriptor.ShaderRegister;
		space = param.Descriptor.RegisterSpace;
		break;
	case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
		assert(param.DescriptorTable.NumDescriptorRanges > 0);
		reg   = param.DescriptorTable.pDescriptorRanges[0].BaseShaderRegister;
		space = param.DescriptorTable.pDescriptorRanges[0].RegisterSpace;
		break;
	}
	m_RootIndexMap.emplace(MAKELONG(space, reg), rootIndex);

	return rootIndex;
}

DescriptorTable& DescriptorTable::AddCBVRange(u32 reg, u32 space, u32 numDescriptors, D3D12_DESCRIPTOR_RANGE_FLAGS flags, u32 offset)
{
	AddDescriptorRange(reg, space, numDescriptors, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, flags, offset);
	return *this;
}

DescriptorTable& DescriptorTable::AddSRVRange(u32 reg, u32 space, u32 numDescriptors, D3D12_DESCRIPTOR_RANGE_FLAGS flags, u32 offset)
{
	AddDescriptorRange(reg, space, numDescriptors, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, flags, offset);
	return *this;
}

DescriptorTable& DescriptorTable::AddUAVRange(u32 reg, u32 space, u32 numDescriptors, D3D12_DESCRIPTOR_RANGE_FLAGS flags, u32 offset)
{
	AddDescriptorRange(reg, space, numDescriptors, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, flags, offset);
	return *this;
}

DescriptorTable& DescriptorTable::AddSamplerRange(u32 reg, u32 space, u32 numDescriptors, D3D12_DESCRIPTOR_RANGE_FLAGS flags, u32 offset)
{
	AddDescriptorRange(reg, space, numDescriptors, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, flags, offset);
	return *this;
}

void DescriptorTable::AddDescriptorRange(u32 reg, u32 space, u32 numDescriptors, D3D12_DESCRIPTOR_RANGE_TYPE type, D3D12_DESCRIPTOR_RANGE_FLAGS flags, u32 offset)
{
	CD3DX12_DESCRIPTOR_RANGE1& range = m_Ranges.emplace_back();
	range.Init(type, numDescriptors, reg, space, flags, offset);
}

}