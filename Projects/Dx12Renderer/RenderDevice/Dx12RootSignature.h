#pragma once

namespace dx12
{

constexpr u32 MAX_ROOT_INDEX = D3D12_MAX_ROOT_COST;

class DescriptorTable
{
public:
	DescriptorTable& AddCBVRange(
		u32 reg, u32 space, u32 numDescriptors,
		D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
		u32 offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);
	DescriptorTable& AddSRVRange(
		u32 reg, u32 space, u32 numDescriptors,
		D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
		u32 offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);
	DescriptorTable& AddUAVRange(
		u32 reg, u32 space, u32 numDescriptors,
		D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
		u32 offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);

	DescriptorTable& AddSamplerRange(
		u32 reg, u32 space, u32 numDescriptors,
		D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
		u32 offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);

	[[nodiscard]]
	size_t Size() const { return m_Ranges.size(); }
	[[nodiscard]]
	const D3D12_DESCRIPTOR_RANGE1* Data() const { return m_Ranges.data(); }

private:
	void AddDescriptorRange(
		u32 reg, u32 space, u32 numDescriptors,
		D3D12_DESCRIPTOR_RANGE_TYPE type, D3D12_DESCRIPTOR_RANGE_FLAGS flags, u32 offset);

private:
	std::vector< CD3DX12_DESCRIPTOR_RANGE1 > m_Ranges;
};

class RootSignatureDesc
{

};

class RootSignature
{
public:
	RootSignature(RenderDevice& device, const std::wstring& name);
	~RootSignature();

	u32 AddConstants(u32 reg, u32 space, u32 numConstants, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	u32 AddCBV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	u32 AddSRV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	u32 AddUAV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	u32 AddSampler(
		UINT                       shaderRegister,
		UINT                       registerSpace,
		D3D12_FILTER               filter,
		D3D12_TEXTURE_ADDRESS_MODE addressUVW,
		UINT                       maxAnisotropy,
		D3D12_COMPARISON_FUNC      comparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
		D3D12_STATIC_BORDER_COLOR  borderColor    = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);
	u32 AddDescriptorTable(const DescriptorTable& table, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

	void Build();

public:
	void CopySignatureParams(RootSignature& srcSignature);

	ID3D12RootSignature* GetD3D12RootSignature() const { return m_d3d12RootSignature; }

	u32 GetNumDescriptors(u32 rootIndex) const;
	u32 GetNumParameters() const { return static_cast<u32>(m_RootParameters.size()); }
	u64 GetDescriptorTableBitMask(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

	const std::vector< CD3DX12_ROOT_PARAMETER1 >& GetParameters() const { return m_RootParameters; }

protected:
	u32 AddParameter(const CD3DX12_ROOT_PARAMETER1& param);

private:
	RenderDevice& m_RenderDevice;
	std::wstring  m_Name;

	ID3D12RootSignature* m_d3d12RootSignature = nullptr;

	std::vector< CD3DX12_ROOT_PARAMETER1 >	   m_RootParameters;
	std::vector< CD3DX12_STATIC_SAMPLER_DESC > m_StaticSamplers;

	std::vector< u32 >             m_DescriptorTableIndices;
	std::vector< DescriptorTable > m_DescriptorTables;

	u32	m_NumDescriptorsPerTable[MAX_ROOT_INDEX] = {};
	u64	m_SamplerTableBitMask    = 0;
	u64	m_DescriptorTableBitMask = 0;
};

}