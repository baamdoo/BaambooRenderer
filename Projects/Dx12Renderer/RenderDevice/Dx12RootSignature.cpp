#include "RendererPch.h"
#include "Dx12RootSignature.h"

namespace dx12
{

RootSignature::RootSignature(RenderDevice& device)
	: m_RenderDevice(device)
{
}

RootSignature::~RootSignature()
{
	COM_RELEASE(m_d3d12RootSignature);

	m_SamplerTableBitMask = 0;
	m_DescriptorTableBitMask = 0;
}

u32 RootSignature::AddConstants(u32 reg, u32 space, u32 numConstants, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1 param = {};
	param.InitAsConstants(numConstants, reg, space, visibility);
	return AddParameter(param);
}

u32 RootSignature::AddCBV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1 param = {};
	param.InitAsConstantBufferView(reg, space, flags, visibility);
	return AddParameter(param);
}

u32 RootSignature::AddSRV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1 param = {};
	param.InitAsShaderResourceView(reg, space, flags, visibility);
	return AddParameter(param);
}

u32 RootSignature::AddUAV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1 param = {};
	param.InitAsUnorderedAccessView(reg, space, flags, visibility);
	return AddParameter(param);
}

u32 RootSignature::AddSampler(
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

u32 RootSignature::AddDescriptorTable(const DescriptorTable& table, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER1 param = {};
	param.InitAsDescriptorTable(static_cast<u32>(table.Size()), nullptr, visibility);
	m_DescriptorTableIndices.push_back(static_cast<u32>(m_DescriptorTables.size()));
	m_DescriptorTables.push_back(table);
	return AddParameter(param);
}

void RootSignature::Build()
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
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	D3D_ROOT_SIGNATURE_VERSION highestVersion = m_RenderDevice.GetHighestRootSignatureVersion();
	ID3DBlob* d3dSignature = nullptr;
	ID3DBlob* d3dError = nullptr;
	ThrowIfFailed(
		D3DX12SerializeVersionedRootSignature(&rootSigDesc, highestVersion, &d3dSignature, &d3dError)
	);

	ThrowIfFailed(d3d12Device->CreateRootSignature(
		0, d3dSignature->GetBufferPointer(), d3dSignature->GetBufferSize(), IID_PPV_ARGS(&m_d3d12RootSignature))
	);

	COM_RELEASE(d3dSignature);
	COM_RELEASE(d3dError);
}

u32 RootSignature::GetNumDescriptors(u32 rootIndex) const
{
	assert(rootIndex < MAX_ROOT_INDEX);
	return m_NumDescriptorsPerTable[rootIndex];
}

u64 RootSignature::GetDescriptorTableBitMask(D3D12_DESCRIPTOR_HEAP_TYPE type) const
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

u32 RootSignature::AddParameter(const CD3DX12_ROOT_PARAMETER1& param)
{
	m_RootParameters.emplace_back(param);
	return static_cast<u32>(m_RootParameters.size()) - 1;
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