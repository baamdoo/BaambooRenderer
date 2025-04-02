#pragma once

namespace dx12
{

constexpr u32 MAX_ROOT_INDEX = D3D12_MAX_ROOT_COST;

class DescriptorTable
{
public:
	void AddCBVRange(
		u32 reg, u32 space, u32 numDescriptors,
		D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
		u32 offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);
	void AddSRVRange(
		u32 reg, u32 space, u32 numDescriptors,
		D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
		u32 offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);
	void AddUAVRange(
		u32 reg, u32 space, u32 numDescriptors,
		D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
		u32 offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);

	void AddSamplerRange(
		u32 reg, u32 space, u32 numDescriptors,
		D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
		u32 offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);

private:
	void AddDescriptorRange(
		u32 reg, u32 space, u32 numDescriptors,
		D3D12_DESCRIPTOR_RANGE_TYPE type, D3D12_DESCRIPTOR_RANGE_FLAGS flags, u32 offset);

public:
	const D3D12_DESCRIPTOR_RANGE1* operator*() const { return m_Ranges.data(); }
	size_t Size() const { return m_Ranges.size(); }

private:
	std::vector< CD3DX12_DESCRIPTOR_RANGE1 > m_Ranges;
};

class RootSignature
{
public:
	RootSignature(RenderContext& context);
	~RootSignature();

	void AddConstants(u32 reg, u32 space, u32 numConstants, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	void AddCBV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	void AddSRV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	void AddUAV(u32 reg, u32 space, D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	void AddDescriptorTable(const DescriptorTable& table, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

	void Build();

public:
	ID3D12RootSignature* GetD3D12RootSignature() const { return m_d3d12RootSignature; }

	u32 GetNumDescriptors(u32 rootIndex) const;
	u32 GetNumParameters() const { return static_cast<u32>(m_RootParameters.size()); }
	u64 GetDescriptorTableBitMask(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

	const std::vector< CD3DX12_ROOT_PARAMETER1 >& GetParameters() const { return m_RootParameters; }

protected:
	void AddParameter(const CD3DX12_ROOT_PARAMETER1& param);
	void AddStaticParameters();

private:
	RenderContext& m_RenderContext;

	ID3D12RootSignature* m_d3d12RootSignature = nullptr;

	std::vector< CD3DX12_ROOT_PARAMETER1 >		m_RootParameters;
	std::vector< CD3DX12_STATIC_SAMPLER_DESC >	m_StaticSamplers;

	u32	m_NumDescriptorsPerTable[MAX_ROOT_INDEX] = {};
	u64	m_SamplerTableBitMask = 0;
	u64	m_DescriptorTableBitMask = 0;
};

}