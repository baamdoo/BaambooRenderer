#include "RendererPch.h"
#include "VkForwardModule.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSceneResource.h"

namespace vk
{

ForwardModule::ForwardModule(RenderDevice& device)
	: Super(device)
{
	auto pAttachment0 =
		Texture::Create(
			m_RenderDevice,
			"ForwardPass::Attachment0",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = VK_FORMAT_R32G32B32A32_SFLOAT,
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			});
	auto pAttachmentDepth =
		Texture::Create(
			m_RenderDevice,
			"ForwardPass::AttachmentDepth",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = VK_FORMAT_D32_SFLOAT,
				.imageUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
			});
	m_pRenderTarget = new RenderTarget(m_RenderDevice, "ForwardPass::RenderPass");
	m_pRenderTarget->AttachTexture(eAttachmentPoint::Color0, pAttachment0)
		            .AttachTexture(eAttachmentPoint::DepthStencil, pAttachmentDepth).Build();

	auto hVS = Shader::Create(m_RenderDevice, "PBRLightingVS", { .filepath = SPIRV_PATH.string() + "PBRLighting.vert.spv" });
	auto hFS = Shader::Create(m_RenderDevice, "PBRLightingPS", { .filepath = SPIRV_PATH.string() + "PBRLighting.frag.spv" });
	m_pGraphicsPipeline = new GraphicsPipeline(m_RenderDevice, "ForwardPSO");
	m_pGraphicsPipeline->SetShaders(hVS, hFS).SetRenderTarget(*m_pRenderTarget).SetDepthWriteEnable(true).Build();

	m_RenderDevice.SetMainRenderPass(m_pRenderTarget->vkRenderPass());
}

ForwardModule::~ForwardModule()
{
	RELEASE(m_pGraphicsPipeline);
	RELEASE(m_pRenderTarget);
}

void ForwardModule::Apply(CommandContext& context)
{
	context.BeginRenderPass(*m_pRenderTarget);
	context.SetRenderPipeline(m_pGraphicsPipeline);

	context.SetGraphicsDynamicUniformBuffer(0, g_FrameData.camera);

	context.DrawIndexedIndirect(*g_FrameData.pSceneResource);

	m_pRenderTarget->InvalidateImageLayout();

	g_FrameData.pColor = m_pRenderTarget->Attachment(eAttachmentPoint::Color0);
	g_FrameData.pDepth = m_pRenderTarget->Attachment(eAttachmentPoint::DepthStencil);
}

void ForwardModule::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pRenderTarget)
		m_pRenderTarget->Resize(width, height, depth);
}

} // namespace vk
