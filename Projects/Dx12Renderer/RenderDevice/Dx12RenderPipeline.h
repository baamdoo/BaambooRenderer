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
class Dx12GraphicsPipeline : public render::GraphicsPipeline
{
public:
	Dx12GraphicsPipeline(Dx12RenderDevice& rd, const std::string& name);
	~Dx12GraphicsPipeline();

	virtual render::GraphicsPipeline& SetRenderTarget(Arc< render::RenderTarget > pRenderTarget) override;

	virtual render::GraphicsPipeline& SetFillMode(bool bWireframe) override;
	virtual render::GraphicsPipeline& SetCullMode(render::eCullMode cullMode) override;

	virtual render::GraphicsPipeline& SetTopology(render::ePrimitiveTopology topology) override;
	virtual render::GraphicsPipeline& SetDepthTestEnable(bool bEnable, render::eCompareOp) override;
	virtual render::GraphicsPipeline& SetDepthWriteEnable(bool bEnable, render::eCompareOp) override;

	virtual render::GraphicsPipeline& SetLogicOp(render::eLogicOp logicOp) override;
	virtual render::GraphicsPipeline& SetBlendEnable(u32 renderTargetIndex, bool bEnable) override;
	virtual render::GraphicsPipeline& SetColorBlending(u32 renderTargetIndex, render::eBlendFactor srcBlend, render::eBlendFactor dstBlend, render::eBlendOp blendOp) override;
	virtual render::GraphicsPipeline& SetAlphaBlending(u32 renderTargetIndex, render::eBlendFactor srcBlend, render::eBlendFactor dstBlend, render::eBlendOp blendOp) override;

	virtual void Build() override;

	Arc< Dx12RootSignature > GetRootSignature() const { return m_pRootSignature; }
	u32 GetConstantRootIndex() const { return m_ConstantRootIndex; }
	inline ID3D12PipelineState* GetD3D12PipelineState() const { return m_d3d12PipelineState; }

protected:
	void SetVertexInputLayout(ID3D12ShaderReflection* d3d12ShaderReflection);
	void AppendRootSignature(const Dx12Shader::ShaderReflection& reflection, D3D12_SHADER_VISIBILITY visibility);

private:
	Dx12RenderDevice& m_RenderDevice;

	Arc< Dx12RootSignature > m_pRootSignature;
	ID3D12PipelineState*     m_d3d12PipelineState = nullptr;

	u32 m_ConstantRootIndex = INVALID_INDEX;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC      m_PipelineDesc = {};
	std::vector< D3D12_INPUT_ELEMENT_DESC > m_InputLayoutDesc;
};


//-------------------------------------------------------------------------
// Compute pipeline
//-------------------------------------------------------------------------
class Dx12ComputePipeline : public render::ComputePipeline
{
public: 
	Dx12ComputePipeline(Dx12RenderDevice& rd, const std::string& name);
	~Dx12ComputePipeline();

	virtual void Build() override;

	Arc< Dx12RootSignature > GetRootSignature() const { return m_pRootSignature; }
	u32 GetConstantRootIndex() const { return m_ConstantRootIndex; }
	inline ID3D12PipelineState* GetD3D12PipelineState() const { return m_d3d12PipelineState; }

private:
	void AppendRootSignature(const Dx12Shader::ShaderReflection& reflection);

private:
	Dx12RenderDevice& m_RenderDevice;

	Arc< Dx12RootSignature > m_pRootSignature;
	ID3D12PipelineState*     m_d3d12PipelineState = nullptr;

	u32 m_ConstantRootIndex = INVALID_INDEX;

	D3D12_COMPUTE_PIPELINE_STATE_DESC m_PipelineDesc = {};
};

}
