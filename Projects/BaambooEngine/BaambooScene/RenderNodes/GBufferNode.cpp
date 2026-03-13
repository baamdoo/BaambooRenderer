#include "BaambooPch.h"
#include "GBufferNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{

GBufferNode::GBufferNode(render::RenderDevice& rd)
	: Super(rd, "GBufferPass")
{
	using namespace render;

	auto pAttachment0 =
		Texture::Create(
			m_RenderDevice,
			"GBufferPass::Attachment0/RGB_Albedo/A_AO",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = eFormat::RGBA8_UNORM,
				.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample | eTextureUsage_TransferSource
			});
	auto pAttachment1 =
		Texture::Create(
			m_RenderDevice,
			"GBufferPass::Attachment1/RGB_Normal/A_MaterialID",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = eFormat::RGBA8_SNORM,
				.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample
			});
	auto pAttachment2 =
		Texture::Create(
			m_RenderDevice,
			"GBufferPass::Attachment2/RGB_Emissive",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = eFormat::RG11B10_UFLOAT,
				.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample
			});
	auto pAttachment3 =
		Texture::Create(
			m_RenderDevice,
			"GBufferPass::Attachment3/RG_Velocity/B_Roughness/A_Metallic",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = eFormat::RGBA16_FLOAT,
				.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample
			});
	auto pAttachmentDepth =
		Texture::Create(
			m_RenderDevice,
			"GBufferPass::AttachmentDepth",
			{
				.resolution      = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format          = eFormat::D32_FLOAT,
				.imageUsage      = eTextureUsage_DepthStencilAttachment | eTextureUsage_Sample,
				.depthClearValue = 0.0f // reversed-z
			});
	m_pRenderTarget = RenderTarget::CreateEmpty(m_RenderDevice, "GBufferPass::RenderPass");
	m_pRenderTarget->AttachTexture(eAttachmentPoint::Color0, pAttachment0)
		            .AttachTexture(eAttachmentPoint::Color1, pAttachment1)
		            .AttachTexture(eAttachmentPoint::Color2, pAttachment2)
		            .AttachTexture(eAttachmentPoint::Color3, pAttachment3)
		            .AttachTexture(eAttachmentPoint::DepthStencil, pAttachmentDepth).Build();

	m_pGBufferPSO = GraphicsPipeline::Create(m_RenderDevice, "GBufferPSO");
	if (!m_RenderDevice.GetDeviceSettings().bMeshShader)
	{
		auto pVS = Shader::Create(m_RenderDevice, "GBufferVS",
			{
				.stage    = eShaderStage::Vertex,
				.filename = "GBufferVS"
			});

		auto pFS = Shader::Create(m_RenderDevice, "GBufferPS",
			{
				.stage    = eShaderStage::Fragment,
				.filename = "GBufferPS"
			});

		m_pGBufferPSO->SetShaders(pVS, pFS)
			          .SetRenderTarget(m_pRenderTarget)
			          .SetDepthWriteEnable(true, eCompareOp::Greater).Build();
	}
	else
	{
		auto pTS = Shader::Create(m_RenderDevice, "GBufferTS",
			{
				.stage    = eShaderStage::Task,
				.filename = "GBufferTS"
			});

		auto pMS = Shader::Create(m_RenderDevice, "GBufferMS",
			{
				.stage    = eShaderStage::Mesh,
				.filename = "GBufferMS"
			});

		auto pFS = Shader::Create(m_RenderDevice, "GBufferPS",
			{
				.stage    = eShaderStage::Fragment,
				.filename = "GBufferTestPS"
			});

		m_pGBufferPSO->SetMeshShaders(pMS, pFS, pTS)
			          .SetRenderTarget(m_pRenderTarget)
			          .SetDepthWriteEnable(true, eCompareOp::Greater).Build();
	}
}

void GBufferNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(renderView);
	using namespace render;

	auto& rm = m_RenderDevice.GetResourceManager();

	context.BeginRenderPass(m_pRenderTarget);
	{
		context.SetRenderPipeline(m_pGBufferPSO.get());

		context.DrawScene(rm.GetSceneResource());
	}
	context.EndRenderPass();

	m_pRenderTarget->InvalidateImageLayout();

	g_FrameData.pColor    = m_pRenderTarget->Attachment(eAttachmentPoint::Color0);
	g_FrameData.pGBuffer0 = m_pRenderTarget->Attachment(eAttachmentPoint::Color0);
	g_FrameData.pGBuffer1 = m_pRenderTarget->Attachment(eAttachmentPoint::Color1);
	g_FrameData.pGBuffer2 = m_pRenderTarget->Attachment(eAttachmentPoint::Color2);
	g_FrameData.pGBuffer3 = m_pRenderTarget->Attachment(eAttachmentPoint::Color3);
	g_FrameData.pDepth    = m_pRenderTarget->Attachment(eAttachmentPoint::DepthStencil);
}

void GBufferNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pRenderTarget)
		m_pRenderTarget->Resize(width, height, depth);
}

} // namespace baamboo