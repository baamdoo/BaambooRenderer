#pragma once

namespace dx12
{

class Dx12RootSignature : public ArcBase
{
public:
	Dx12RootSignature(Dx12RenderDevice& rd, const std::string& name, D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	~Dx12RootSignature();

	u32 AddConstants(u32 reg, u32 space, u32 numConstants, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	u32 AddSamplerPreset(u32 preset);

	void Build();

public:
	ID3D12RootSignature* GetD3D12RootSignature() const { return m_d3d12RootSignature; }

	u32 GetRootIndex(D3D12_ROOT_PARAMETER_TYPE type, u32 space, u32 reg) const;

private:
	Dx12RenderDevice& m_RenderDevice;
	std::string       m_Name;

	ID3D12RootSignature* m_d3d12RootSignature = nullptr;

	std::vector< CD3DX12_ROOT_PARAMETER1 >	   m_RootParameters;
	std::vector< CD3DX12_STATIC_SAMPLER_DESC > m_StaticSamplers;

	using RootKey = std::pair< D3D12_ROOT_PARAMETER_TYPE, LONG >;
	struct RootKeyHash
	{
		std::size_t operator()(const RootKey& key) const
		{
			std::size_t h1 = std::hash<int>{}(static_cast<int>(key.first));
			std::size_t h2 = std::hash<LONG>{}(key.second);

			return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
		}
	};

	std::unordered_map< RootKey, u32, RootKeyHash > m_RootIndexMap;

	D3D12_ROOT_SIGNATURE_FLAGS m_Flags;
};

}