#pragma once
#include "Dx12RootSignature.h"
#include "Dx12DescriptorAllocation.h"
#include "RenderResource/Dx12Shader.h"

namespace dx12
{

class Shader;
class RenderTarget;

//-------------------------------------------------------------------------
// Graphics pipeline
//-------------------------------------------------------------------------
class GraphicsPipeline
{
public:
	GraphicsPipeline(RenderDevice& device, const std::wstring& name);
	~GraphicsPipeline();

	GraphicsPipeline& SetShaderModules(
		Arc< Shader > pVS,
		Arc< Shader > pPS,
		Arc< Shader > pGS = nullptr,
		Arc< Shader > pHS = nullptr,
		Arc< Shader > pDS = nullptr);
	GraphicsPipeline& SetRootSignature(RootSignature* pRootSignature);
	GraphicsPipeline& SetRenderTargetFormats(const RenderTarget& renderTarget);

	GraphicsPipeline& SetCullMode(D3D12_CULL_MODE cullMode);
	GraphicsPipeline& SetFillMode(bool bWireframe);

	GraphicsPipeline& SetBlendEnable(u32 renderTargetIndex, bool bEnable);
	GraphicsPipeline& SetBlendState(u32 renderTargetIndex, D3D12_BLEND srcBlend, D3D12_BLEND dstBlend, D3D12_BLEND srcBlendAlpha, D3D12_BLEND dstBlendAlpha);
	GraphicsPipeline& SetBlendOp(u32 renderTargetIndex, D3D12_BLEND_OP blendOp, D3D12_BLEND_OP blendOpAlpha, D3D12_LOGIC_OP logicOp = D3D12_LOGIC_OP_NOOP);

	GraphicsPipeline& SetTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type);

	void Build();

public:
	inline ID3D12PipelineState* GetD3D12PipelineState() const { return m_d3d12PipelineState; }

protected:
	void SetVertexInputLayout(ID3D12ShaderReflection* d3d12ShaderReflection);

private:
	RenderDevice& m_RenderDevice;
	std::wstring   m_Name;

	Arc< Shader > m_pVS;
	Arc< Shader > m_pPS;
	Arc< Shader > m_pGS;
	Arc< Shader > m_pHS;
	Arc< Shader > m_pDS;

	ID3D12PipelineState* m_d3d12PipelineState = nullptr;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC      m_PipelineDesc = {};
	std::vector< D3D12_INPUT_ELEMENT_DESC > m_InputLayoutDesc;
};


//-------------------------------------------------------------------------
// Compute pipeline
//-------------------------------------------------------------------------
class ComputePipeline
{
public:
	ComputePipeline(RenderDevice& device, const std::wstring& name);
	~ComputePipeline();

	ComputePipeline& SetShaderModules(Arc< Shader > pCS);
	ComputePipeline& SetRootSignature(RootSignature* pRootSignature);

	void Build();

public:
	inline ID3D12PipelineState* GetD3D12PipelineState() const { return m_d3d12PipelineState; }

private:
	RenderDevice& m_RenderDevice;
	std::wstring   m_Name;

	Arc< Shader > m_pCS;

	ID3D12PipelineState* m_d3d12PipelineState = nullptr;

	D3D12_COMPUTE_PIPELINE_STATE_DESC m_PipelineDesc = {};
};

}
