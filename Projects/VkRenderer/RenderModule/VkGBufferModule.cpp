#include "RendererPch.h"
#include "VkGBufferModule.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSceneResource.h"

namespace vk
{

GBufferModule::GBufferModule(RenderDevice& device)
	: Super(device)
{
	auto pAttachment0 =
		Texture::Create(
			m_RenderDevice,
			"GBufferPass::Attachment0/RGB_Albedo/A_AO",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = VK_FORMAT_R8G8B8A8_UNORM,
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			});
	auto pAttachment1 =
		Texture::Create(
			m_RenderDevice,
			"GBufferPass::Attachment1/RGB_Normal/A_MaterialID",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = VK_FORMAT_R8G8B8A8_SNORM,
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			});
	auto pAttachment2 =
		Texture::Create(
			m_RenderDevice,
			"GBufferPass::Attachment2/RGB_Emissive",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = VK_FORMAT_A2R10G10B10_UNORM_PACK32,
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			});
	auto pAttachment3 =
		Texture::Create(
			m_RenderDevice,
			"GBufferPass::Attachment3/RG_Velocity/B_Roughness/A_Metallic",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = VK_FORMAT_R8G8B8A8_SNORM,
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			});
	auto pAttachmentDepth =
		Texture::Create(
			m_RenderDevice,
			"GBufferPass::AttachmentDepth",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = VK_FORMAT_D32_SFLOAT,
				.imageUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			});
	m_pRenderTarget = new RenderTarget(m_RenderDevice, "GBufferPass::RenderPass");
	m_pRenderTarget->AttachTexture(eAttachmentPoint::Color0, pAttachment0)
		            .AttachTexture(eAttachmentPoint::Color1, pAttachment1)
		            .AttachTexture(eAttachmentPoint::Color2, pAttachment2)
		            .AttachTexture(eAttachmentPoint::Color3, pAttachment3)
		            .AttachTexture(eAttachmentPoint::DepthStencil, pAttachmentDepth).Build();

	auto hVS = Shader::Create(m_RenderDevice, "GBufferVS", { .filepath = SPIRV_PATH.string() + "GBuffer.vert.spv" });
	auto hFS = Shader::Create(m_RenderDevice, "GBufferPS", { .filepath = SPIRV_PATH.string() + "GBuffer.frag.spv" });
	m_pGraphicsPipeline = new GraphicsPipeline(m_RenderDevice, "GBufferPSO");
	m_pGraphicsPipeline->SetShaders(hVS, hFS).SetRenderTarget(*m_pRenderTarget).SetDepthWriteEnable(true).Build();
}

GBufferModule::~GBufferModule()
{
	RELEASE(m_pGraphicsPipeline);
	RELEASE(m_pRenderTarget);
}

void GBufferModule::Apply(CommandContext& context)
{
	context.BeginRenderPass(*m_pRenderTarget);
	context.SetRenderPipeline(m_pGraphicsPipeline);

	context.SetGraphicsDynamicUniformBuffer(0, g_FrameData.camera);

	context.DrawIndexedIndirect(*g_FrameData.pSceneResource);

	m_pRenderTarget->InvalidateImageLayout();

	g_FrameData.pGBuffer0 = m_pRenderTarget->Attachment(eAttachmentPoint::Color0);
	g_FrameData.pGBuffer1 = m_pRenderTarget->Attachment(eAttachmentPoint::Color1);
	g_FrameData.pGBuffer2 = m_pRenderTarget->Attachment(eAttachmentPoint::Color2);
	g_FrameData.pGBuffer3 = m_pRenderTarget->Attachment(eAttachmentPoint::Color3);
	g_FrameData.pDepth    = m_pRenderTarget->Attachment(eAttachmentPoint::DepthStencil);

	context.EndRenderPass();
}

void GBufferModule::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pRenderTarget)
		m_pRenderTarget->Resize(width, height, depth);
}

} // namespace vk