#include "RendererPch.h"
#include "Dx12GBufferModule.h"
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

GBufferModule::GBufferModule(RenderDevice& device)
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
	colorClearValue.Format   = texDescColor.Format;
	colorClearValue.Color[0] = 0.0f;
	colorClearValue.Color[1] = 0.0f;
	colorClearValue.Color[2] = 0.0f;
	colorClearValue.Color[3] = 0.0f;

	D3D12_CLEAR_VALUE depthClearValue    = {};
	depthClearValue.Format               = texDescDepth.Format;
	depthClearValue.DepthStencil.Depth   = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	auto attachment0 =
		Texture::Create(
			m_RenderDevice,
			L"GBufferPass::Attachment0/RGB_Albedo/A_AO",
			{
				.desc       = texDescColor,
				.clearValue = colorClearValue
			});

	texDescColor.Format = colorClearValue.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
	auto attachment1 =
		Texture::Create(
			m_RenderDevice,
			L"GBufferPass::Attachment1/RGB_Normal/A_MaterialID",
			{
				.desc       = texDescColor,
				.clearValue = colorClearValue
			});

	texDescColor.Format = colorClearValue.Format = DXGI_FORMAT_R11G11B10_FLOAT;
	auto attachment2 =
		Texture::Create(
			m_RenderDevice,
			L"GBufferPass::Attachment2/RGB_Emissive",
			{
				.desc       = texDescColor,
				.clearValue = colorClearValue
			});

	texDescColor.Format = colorClearValue.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
	auto attachment3 =
		Texture::Create(
			m_RenderDevice,
			L"GBufferPass::Attachment3/RG_Velocity/B_Roughness/A_Metallic",
			{
				.desc       = texDescColor,
				.clearValue = colorClearValue
			});
	auto attachmentDepth =
		Texture::Create(
			m_RenderDevice,
			L"GBufferPass::AttachmentDepth",
			{
				.desc       = texDescDepth,
				.clearValue = depthClearValue
			});
	attachmentDepth->CreateShaderResourceView(
		{
			.Format = DXGI_FORMAT_R32_FLOAT,
			.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels       = 1,
			}
		});
	m_RenderTarget.AttachTexture(eAttachmentPoint::Color0, attachment0)
		          .AttachTexture(eAttachmentPoint::Color1, attachment1)
		          .AttachTexture(eAttachmentPoint::Color2, attachment2)
		          .AttachTexture(eAttachmentPoint::Color3, attachment3)
		          .AttachTexture(eAttachmentPoint::DepthStencil, attachmentDepth);

	m_pRootSignature = g_FrameData.pSceneResource->GetSceneRootSignature();

	// **
	// Pipeline
	// **
	auto pVS = Shader::Create(m_RenderDevice, L"GBufferVS", { .filepath = CSO_PATH.string() + "GBufferVS.cso" });
	auto pPS = Shader::Create(m_RenderDevice, L"GBufferPS", { .filepath = CSO_PATH.string() + "GBufferPS.cso" });
	m_pGraphicsPipeline = new GraphicsPipeline(m_RenderDevice, L"GBufferPSO");
	m_pGraphicsPipeline->SetShaderModules(std::move(pVS), std::move(pPS))
		                .SetRenderTargetFormats(m_RenderTarget)
		                .SetRootSignature(m_pRootSignature).Build();
}

GBufferModule::~GBufferModule()
{
	RELEASE(m_pGraphicsPipeline);
}

void GBufferModule::Apply(CommandContext& context)
{
	m_RenderTarget.ClearTexture(context, eAttachmentPoint::All);
	context.SetRenderTarget(m_RenderTarget);

	context.SetRenderPipeline(m_pGraphicsPipeline);
	context.SetGraphicsRootSignature(m_pRootSignature);
	context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	auto pMaterial  = g_FrameData.pSceneResource->GetMaterialBuffer();
	auto pTransform = g_FrameData.pSceneResource->GetTransformBuffer();
	assert(pTransform && pMaterial);

	context.SetGraphicsDynamicConstantBuffer(2, g_FrameData.camera);
	context.SetGraphicsShaderResourceView(3, pTransform->GpuAddress());
	context.SetGraphicsShaderResourceView(4, pMaterial->GpuAddress());
	context.StageDescriptors(5, 0, std::move(g_FrameData.pSceneResource->sceneTexSRVs));
	context.DrawIndexedIndirect(*g_FrameData.pSceneResource);

	g_FrameData.pGBuffer0 = m_RenderTarget.Attachment(eAttachmentPoint::Color0);
	g_FrameData.pGBuffer1 = m_RenderTarget.Attachment(eAttachmentPoint::Color1);
	g_FrameData.pGBuffer2 = m_RenderTarget.Attachment(eAttachmentPoint::Color2);
	g_FrameData.pGBuffer3 = m_RenderTarget.Attachment(eAttachmentPoint::Color3);
	g_FrameData.pDepth    = m_RenderTarget.Attachment(eAttachmentPoint::DepthStencil);
}

void GBufferModule::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(depth);
	m_RenderTarget.Resize(width, height);
}

} // namespace dx12