#pragma once

namespace dx12
{

class RootSignature;

class CommandSignatureDesc
{
public:
	explicit CommandSignatureDesc(size_t numParameters, UINT stride)
		: m_Stride(stride)
	{
		m_Parameters.reserve(numParameters);
	}
	D3D12_COMMAND_SIGNATURE_DESC Build() noexcept;

	CommandSignatureDesc& AddDraw();
	CommandSignatureDesc& AddDrawIndexed();
	CommandSignatureDesc& AddDispatch();

	CommandSignatureDesc& AddVertexBufferView(UINT slot);
	CommandSignatureDesc& AddIndexBufferView();

	CommandSignatureDesc& AddConstant(UINT rootIndex, UINT dstOffsetIn32BitValues, UINT num32BitValuesToSet);
	CommandSignatureDesc& AddConstantBufferView(UINT rootIndex);
	CommandSignatureDesc& AddShaderResourceView(UINT rootIndex);
	CommandSignatureDesc& AddUnorderedAccessView(UINT rootIndex);

	CommandSignatureDesc& AddDispatchMesh();

	CommandSignatureDesc& AddDispatchRays();

private:
	std::vector<D3D12_INDIRECT_ARGUMENT_DESC> m_Parameters;
	UINT									  m_Stride = 0;
};

class CommandSignature
{
public:
	CommandSignature() noexcept = default;
	explicit CommandSignature(RenderContext& context, CommandSignatureDesc& signatureDesc, ID3D12RootSignature* d3d12RootSignature);
	~CommandSignature();

	inline ID3D12CommandSignature* GetD3D12CommandSignature() const noexcept { return m_d3d12CommandSignature; }

private:
	ID3D12CommandSignature* m_d3d12CommandSignature;
};

}