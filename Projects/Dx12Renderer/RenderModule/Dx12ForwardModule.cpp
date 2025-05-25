#include "RendererPch.h"
#include "Dx12ForwardModule.h"
#include "RenderDevice/Dx12CommandList.h"
#include "RenderDevice/Dx12RootSignature.h"
#include "RenderDevice/Dx12RenderPipeline.h"
#include "RenderDevice/Dx12ResourceManager.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12RenderTarget.h"
#include "RenderResource/Dx12SceneResource.h"

namespace dx12
{

ForwardModule::ForwardModule(RenderContext& context)
	: Super(context)
{
	auto& rm = m_RenderContext.GetResourceManager();
	CD3DX12_RESOURCE_DESC texDescColor =
		CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM,
			m_RenderContext.WindowWidth(), m_RenderContext.WindowHeight(), 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);
	CD3DX12_RESOURCE_DESC texDescDepth =
		CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_D32_FLOAT,
			m_RenderContext.WindowWidth(), m_RenderContext.WindowHeight(), 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);

	D3D12_CLEAR_VALUE colorClearValue = {};
	colorClearValue.Format = texDescColor.Format;
	colorClearValue.Color[0] = 0.0f;
	colorClearValue.Color[1] = 0.0f;
	colorClearValue.Color[2] = 0.0f;
	colorClearValue.Color[3] = 0.0f;

	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.Format = texDescDepth.Format;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	auto attachment0 =
		rm.Create< Texture >(
			L"ForwardPass::Attachment0",
			{
				.desc = texDescColor,
				.clearValue = colorClearValue
			});
	auto attachmentDepth =
		rm.Create< Texture >(
			L"ForwardPass::AttachmentDepth",
			{
				.desc = texDescDepth,
				.clearValue = depthClearValue
			});
	m_RenderTarget.AttachTexture(eAttachmentPoint::Color0, rm.Get(attachment0))
		          .AttachTexture(eAttachmentPoint::DepthStencil, rm.Get(attachmentDepth));


	m_pRootSignature = g_FrameData.pSceneResource->GetSceneRootSignature();

	// **
	// Pipeline
	// **
	auto hVS = rm.Create< Shader >(L"SimpleModelVS", Shader::CreationInfo{ .filepath = CSO_PATH.string() + "SimpleModelVS.cso" });
	auto hPS = rm.Create< Shader >(L"SimpleModelPS", Shader::CreationInfo{ .filepath = CSO_PATH.string() + "SimpleModelPS.cso" });
	m_pGraphicsPipeline = new GraphicsPipeline(m_RenderContext, L"ForwardPSO");
	m_pGraphicsPipeline->SetShaderModules(hVS, hPS)
		                .SetRenderTargetFormats(m_RenderTarget)
		                .SetRootSignature(m_pRootSignature).Build();
}

ForwardModule::~ForwardModule()
{
	RELEASE(m_pGraphicsPipeline);
}

void ForwardModule::Apply(CommandList& cmdList)
{
	m_RenderTarget.ClearTexture(cmdList, eAttachmentPoint::All);
	cmdList.SetRenderTarget(m_RenderTarget);

	cmdList.SetPipelineState(m_pGraphicsPipeline);
	cmdList.SetGraphicsRootSignature(m_pRootSignature);
	cmdList.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cmdList.SetGraphicsDynamicConstantBuffer(2, g_FrameData.camera);
	cmdList.StageDescriptors(5, static_cast<u32>(g_FrameData.pSceneResource->srvs.size()), 0, *(g_FrameData.pSceneResource->srvs.data()));
	cmdList.DrawIndexedIndirect(*g_FrameData.pSceneResource);

	g_FrameData.pColor = m_RenderTarget.Attachment(eAttachmentPoint::Color0);
	g_FrameData.pDepth = m_RenderTarget.Attachment(eAttachmentPoint::DepthStencil);
}

void ForwardModule::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(depth);
	m_RenderTarget.Resize(width, height);
}

} // namespace dx12