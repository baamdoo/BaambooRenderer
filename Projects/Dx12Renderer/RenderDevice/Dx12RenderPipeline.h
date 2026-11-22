#pragma once
#include "Dx12RootSignature.h"
#include "RenderResource/Dx12Shader.h"

namespace dx12
{

//-------------------------------------------------------------------------
// Graphics Pipeline
//-------------------------------------------------------------------------
class Dx12GraphicsPipeline : public render::GraphicsPipeline
{
public:
	Dx12GraphicsPipeline(Dx12RenderDevice& rd, const char* name);
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

	virtual bool Build() override;
	virtual bool IsBuilt() const override { return m_d3d12PipelineState != nullptr; }

	Arc< Dx12RootSignature > GetRootSignature() const { return m_pRootSignature; }
	inline ID3D12PipelineState* GetD3D12PipelineState() const { return m_d3d12PipelineState; }
	inline D3D_PRIMITIVE_TOPOLOGY GetD3D12PrimitiveTopology() const { return m_D3DPrimitiveTopology; }

private:
	void ParseRootParameters(const render::ShaderBindingLayout& bindings);

private:
	Dx12RenderDevice& m_RenderDevice;

	Arc< Dx12RootSignature > m_pRootSignature;
	ID3D12PipelineState*     m_d3d12PipelineState = nullptr;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC m_PipelineDesc = {};

	D3D_PRIMITIVE_TOPOLOGY m_D3DPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	struct MeshPipelineStream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_MS                    MS;
		CD3DX12_PIPELINE_STATE_STREAM_AS                    AS;
		CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
		CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER            RasterizerState;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencilState;
		CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC            BlendState;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
		CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC           SampleDesc;
	};
};


//-------------------------------------------------------------------------
// Compute Pipeline
//-------------------------------------------------------------------------
class Dx12ComputePipeline : public render::ComputePipeline
{
public: 
	Dx12ComputePipeline(Dx12RenderDevice& rd, const char* name);
	~Dx12ComputePipeline();

	virtual bool Build() override;
	virtual bool IsBuilt() const override { return m_d3d12PipelineState != nullptr; }

	Arc< Dx12RootSignature > GetRootSignature() const { return m_pRootSignature; }
	inline ID3D12PipelineState* GetD3D12PipelineState() const { return m_d3d12PipelineState; }

private:
	void ParseRootParameters(const render::ShaderBindingLayout& bindings);

private:
	Dx12RenderDevice& m_RenderDevice;

	Arc< Dx12RootSignature > m_pRootSignature;
	ID3D12PipelineState*     m_d3d12PipelineState = nullptr;

	D3D12_COMPUTE_PIPELINE_STATE_DESC m_PipelineDesc = {};
};


//-------------------------------------------------------------------------
// DXR Pipeline
//-------------------------------------------------------------------------
class Dx12RaytracingPipeline : public render::RaytracingPipeline
{
public:
	Dx12RaytracingPipeline(Dx12RenderDevice& rd, const char* name);
	~Dx12RaytracingPipeline();

	virtual bool Build() override;
	virtual bool IsBuilt() const override { return m_d3d12StateObject != nullptr; }

	virtual const void* GetShaderIdentifier(const std::string& exportName) const override;

	[[nodiscard]]
	Arc< Dx12RootSignature > GetGlobalRootSignature() const { return m_pGlobalRootSignature; }
	[[nodiscard]]
	ID3D12StateObject* GetD3D12StateObject() const { return m_d3d12StateObject; }

private:
	bool BuildStateObject();
	void ParseRootParameters(const render::ShaderBindingLayout& bindings);

private:
	Dx12RenderDevice& m_RenderDevice;

	Arc< Dx12RootSignature > m_pGlobalRootSignature;

	ID3D12StateObject*           m_d3d12StateObject           = nullptr;
	ID3D12StateObjectProperties* m_d3d12StateObjectProperties = nullptr;
};


}
