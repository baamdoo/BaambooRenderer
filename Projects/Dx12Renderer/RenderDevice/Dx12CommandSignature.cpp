#include "RendererPch.h"
#include "Dx12CommandSignature.h"

namespace dx12
{

D3D12_COMMAND_SIGNATURE_DESC CommandSignatureDesc::Build() noexcept
{
	D3D12_COMMAND_SIGNATURE_DESC Desc = {};
	Desc.ByteStride = m_Stride;
	Desc.NumArgumentDescs = static_cast<UINT>(m_Parameters.size());
	Desc.pArgumentDescs = m_Parameters.data();
	Desc.NodeMask = 0;
	return Desc;
}

CommandSignatureDesc& CommandSignatureDesc::AddDraw()
{
	D3D12_INDIRECT_ARGUMENT_DESC& Desc = m_Parameters.emplace_back();
	Desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

	return *this;
}

CommandSignatureDesc& CommandSignatureDesc::AddDrawIndexed()
{
	D3D12_INDIRECT_ARGUMENT_DESC& Desc = m_Parameters.emplace_back();
	Desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

	return *this;
}

CommandSignatureDesc& CommandSignatureDesc::AddDispatch()
{
	D3D12_INDIRECT_ARGUMENT_DESC& Desc = m_Parameters.emplace_back();
	Desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

	return *this;
}

CommandSignatureDesc& CommandSignatureDesc::AddVertexBufferView(UINT slot)
{
	D3D12_INDIRECT_ARGUMENT_DESC& Desc = m_Parameters.emplace_back();
	Desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
	Desc.VertexBuffer.Slot = slot;

	return *this;
}

CommandSignatureDesc& CommandSignatureDesc::AddIndexBufferView()
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_Parameters.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;

	return *this;
}

CommandSignatureDesc& CommandSignatureDesc::AddConstant(UINT rootIndex, UINT dstOffsetIn32BitValues, UINT num32BitValues)
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_Parameters.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
	desc.Constant.RootParameterIndex = rootIndex;
	desc.Constant.DestOffsetIn32BitValues = dstOffsetIn32BitValues;
	desc.Constant.Num32BitValuesToSet = num32BitValues;

	return *this;
}

CommandSignatureDesc& CommandSignatureDesc::AddConstantBufferView(UINT rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC& Desc = m_Parameters.emplace_back();
	Desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
	Desc.ConstantBufferView.RootParameterIndex = rootIndex;

	return *this;
}

CommandSignatureDesc& CommandSignatureDesc::AddShaderResourceView(UINT rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC& Desc = m_Parameters.emplace_back();
	Desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
	Desc.ShaderResourceView.RootParameterIndex = rootIndex;

	return *this;
}

CommandSignatureDesc& CommandSignatureDesc::AddUnorderedAccessView(UINT rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC& Desc = m_Parameters.emplace_back();
	Desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
	Desc.UnorderedAccessView.RootParameterIndex = rootIndex;

	return *this;
}

CommandSignatureDesc& CommandSignatureDesc::AddDispatchMesh()
{
	D3D12_INDIRECT_ARGUMENT_DESC& Desc = m_Parameters.emplace_back();
	Desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

	return *this;
}

CommandSignatureDesc& CommandSignatureDesc::AddDispatchRays()
{
	D3D12_INDIRECT_ARGUMENT_DESC& Desc = m_Parameters.emplace_back();
	Desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS;

	return *this;
}

CommandSignature::CommandSignature(RenderDevice& device, CommandSignatureDesc& signatureDesc, ID3D12RootSignature* d3d12RootSignature)
{
	auto d3d12Device = device.GetD3D12Device();

	D3D12_COMMAND_SIGNATURE_DESC desc = signatureDesc.Build();
	DX_CHECK(d3d12Device->CreateCommandSignature(
		&desc, 
		d3d12RootSignature, 
		IID_PPV_ARGS(&m_d3d12CommandSignature)));
}

CommandSignature::~CommandSignature()
{
	COM_RELEASE(m_d3d12CommandSignature);
}

} // namespace dx12