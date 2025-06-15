#include "RendererPch.h"
#include "Dx12ForwardModule.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderDevice/Dx12RootSignature.h"
#include "RenderDevice/Dx12RenderPipeline.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12RenderTarget.h"
#include "RenderResource/Dx12SceneResource.h"

namespace dx12
{

ForwardModule::ForwardModule(RenderDevice& device)
	: Super(device)
{
	CD3DX12_RESOURCE_DESC texDescColor =
		CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM,
			m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);
	CD3DX12_RESOURCE_DESC texDescDepth =
		CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_D32_FLOAT,
			m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1, 1, 1, 0,
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
		Texture::Create(
			m_RenderDevice,
			L"ForwardPass::Attachment0",
			{
				.desc       = texDescColor,
				.clearValue = colorClearValue
			});
	auto attachmentDepth =
		Texture::Create(
			m_RenderDevice,
			L"ForwardPass::AttachmentDepth",
			{
				.desc       = texDescDepth,
				.clearValue = depthClearValue
			});
	m_RenderTarget.AttachTexture(eAttachmentPoint::Color0, attachment0)
		          .AttachTexture(eAttachmentPoint::DepthStencil, attachmentDepth);

	m_pRootSignature = g_FrameData.pSceneResource->GetSceneRootSignature();

	// **
	// Pipeline
	// **
	auto vs = Shader::Create(m_RenderDevice, L"PBRLightingVS", { .filepath = CSO_PATH.string() + "PBRLightingVS.cso" });
	auto ps = Shader::Create(m_RenderDevice, L"PBRLightingPS", { .filepath = CSO_PATH.string() + "PBRLightingPS.cso" });
	m_pGraphicsPipeline = new GraphicsPipeline(m_RenderDevice, L"ForwardPSO");
	m_pGraphicsPipeline->SetShaderModules(std::move(vs), std::move(ps))
		                .SetRenderTargetFormats(m_RenderTarget)
		                .SetRootSignature(m_pRootSignature).Build();
}

ForwardModule::~ForwardModule()
{
	RELEASE(m_pGraphicsPipeline);
}

void ForwardModule::Apply(CommandContext& context)
{
	m_RenderTarget.ClearTexture(context, eAttachmentPoint::All);
	context.SetRenderTarget(m_RenderTarget);

	context.SetRenderPipeline(m_pGraphicsPipeline);
	context.SetGraphicsRootSignature(m_pRootSignature);
	context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	auto pLight     = g_FrameData.pSceneResource->GetLightBuffer();
	auto pMaterial  = g_FrameData.pSceneResource->GetMaterialBuffer();
	auto pTransform = g_FrameData.pSceneResource->GetTransformBuffer();
	assert(pTransform && pMaterial);

	context.SetGraphicsDynamicConstantBuffer(2, g_FrameData.camera);
	context.SetGraphicsConstantBufferView(3, pLight->GpuAddress());
	context.SetGraphicsShaderResourceView(4, pTransform->GpuAddress());
	context.SetGraphicsShaderResourceView(5, pMaterial->GpuAddress());
	context.StageDescriptors(6, static_cast<u32>(g_FrameData.pSceneResource->srvs.size()), 0, *(g_FrameData.pSceneResource->srvs.data()));
	context.DrawIndexedIndirect(*g_FrameData.pSceneResource);

	g_FrameData.pColor = m_RenderTarget.Attachment(eAttachmentPoint::Color0);
	g_FrameData.pDepth = m_RenderTarget.Attachment(eAttachmentPoint::DepthStencil);
}

void ForwardModule::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(depth);
	m_RenderTarget.Resize(width, height);
}

} // namespace dx12