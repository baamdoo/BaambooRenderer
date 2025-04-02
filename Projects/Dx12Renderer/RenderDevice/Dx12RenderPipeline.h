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
	GraphicsPipeline(RenderContext& context, const std::wstring& name);
	~GraphicsPipeline();

	GraphicsPipeline& SetShaderModules(
		baamboo::ResourceHandle< Shader > vs, 
		baamboo::ResourceHandle< Shader > ps, 
		baamboo::ResourceHandle< Shader > gs = baamboo::ResourceHandle< Shader >(), 
		baamboo::ResourceHandle< Shader > hs = baamboo::ResourceHandle< Shader >(),
		baamboo::ResourceHandle< Shader > ds = baamboo::ResourceHandle< Shader >());
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
	RenderContext& m_RenderContext;
	std::wstring   m_Name;

	baamboo::ResourceHandle< Shader > m_hVS;
	baamboo::ResourceHandle< Shader > m_hPS;
	baamboo::ResourceHandle< Shader > m_hGS;
	baamboo::ResourceHandle< Shader > m_hHS;
	baamboo::ResourceHandle< Shader > m_hDS;

	ID3D12PipelineState* m_d3d12PipelineState = nullptr;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC m_d3d12PipelineDesc = {};
	std::vector< D3D12_INPUT_ELEMENT_DESC > m_d3d12InputLayoutDesc;
};


//-------------------------------------------------------------------------
// Compute pipeline
//-------------------------------------------------------------------------
class ComputePipeline
{
public:
	ComputePipeline(RenderContext& context, const std::wstring& name);
	~ComputePipeline();

	void SetShaderModules(baamboo::ResourceHandle< Shader > cs);
	void SetRootSignature(RootSignature* pRootSignature);

	void Build();

public:
	inline ID3D12PipelineState* GetD3D12PipelineState() const { return m_d3d12PipelineState; }

private:
	RenderContext& m_RenderContext;
	std::wstring   m_Name;

	baamboo::ResourceHandle< Shader > m_hCS;
	ID3D12PipelineState* m_d3d12PipelineState = nullptr;

	D3D12_COMPUTE_PIPELINE_STATE_DESC m_d3d12PipelineDesc = {};
};

}
