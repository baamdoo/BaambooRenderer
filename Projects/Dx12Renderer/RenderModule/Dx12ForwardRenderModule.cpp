#include "RendererPch.h"
#include "Dx12ForwardRenderModule.h"
#include "RenderDevice/Dx12RenderContext.h"
#include "RenderDevice/Dx12ResourceManager.h"
#include "RenderDevice/Dx12RenderPipeline.h"
#include "RenderDevice/Dx12CommandList.h"
#include "RenderDevice/Dx12RootSignature.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12RenderTarget.h"

namespace dx12
{

namespace
{
	RenderTarget      _renderTarget;
	RootSignature*    _pSimpleRS = nullptr;
	GraphicsPipeline* _pSimplePSO = nullptr;
}

void ForwardPass::Initialize(RenderContext& renderContext)
{
	auto& rm = renderContext.GetResourceManager();
	CD3DX12_RESOURCE_DESC texDesc =
		CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM,
			renderContext.ViewportWidth(), renderContext.ViewportHeight(), 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);

	D3D12_CLEAR_VALUE colorClearValue = {};
	colorClearValue.Format = texDesc.Format;
	colorClearValue.Color[0] = 0.0f;
	colorClearValue.Color[1] = 0.0f;
	colorClearValue.Color[2] = 0.0f;
	colorClearValue.Color[3] = 0.0f;


	// **
	// Render target
	// **
	auto pAttachment0 =
		rm.Create< Texture >(
			L"ForwardPass::Attachment0",
			{
				.desc = texDesc,
				.clearValue = colorClearValue
			});
	_renderTarget.AttachTexture(eAttachmentPoint::Color0, rm.Get(pAttachment0));


	// **
	// Root signature
	// **
	_pSimpleRS = new RootSignature(renderContext);
	_pSimpleRS->AddCBV(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
	_pSimpleRS->Build();


	// **
	// Pipeline
	// **
	auto hVS = rm.Create< Shader >(L"SimpleTriangleVS", Shader::CreationInfo{ .filepath = CSO_PATH.string() + "SimpleTriangleVS.cso"});
	auto hFS = rm.Create< Shader >(L"SimpleTrianglePS", Shader::CreationInfo{ .filepath = CSO_PATH.string() + "SimpleTrianglePS.cso"});
	_pSimplePSO = new GraphicsPipeline(renderContext, L"ForwardPSO");
	_pSimplePSO->SetShaderModules(hVS, hFS).SetRenderTargetFormats(_renderTarget).SetRootSignature(_pSimpleRS).Build();
}

void ForwardPass::Apply(CommandList& cmdList, float4 testColor)
{
	_renderTarget.ClearTexture(cmdList, eAttachmentPoint::All);
	cmdList.SetRenderTarget(_renderTarget);

	cmdList.SetPipelineState(_pSimplePSO);
	cmdList.SetGraphicsRootSignature(_pSimpleRS);
	cmdList.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cmdList.SetGraphicsDynamicConstantBuffer(0, testColor);

	cmdList.Draw(3);
}

void ForwardPass::Destroy()
{
	RELEASE(_pSimplePSO);
	RELEASE(_pSimpleRS);
}

void ForwardPass::Resize(u32 width, u32 height, u32 depth)
{
	_renderTarget.Resize(width, height);
}

Texture* ForwardPass::GetRenderedTexture(eAttachmentPoint attachment)
{
	return _renderTarget.Attachment(attachment);
}

} // namespace dx12