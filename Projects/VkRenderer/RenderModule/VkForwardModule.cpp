#include "RendererPch.h"
#include "VkForwardModule.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandBuffer.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkRenderTarget.h"

namespace vk
{

ForwardModule::ForwardModule(RenderContext& context)
	: Super(context)
{
	auto& rm = m_RenderContext.GetResourceManager();
	auto pAttachment0 =
		rm.Create< Texture >(
			L"ForwardPass::Attachment0",
			{
				.resolution = { m_RenderContext.WindowWidth(), m_RenderContext.WindowHeight(), 1 },
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			});
	auto pAttachmentDepth =
		rm.Create< Texture >(
			L"ForwardPass::AttachmentDepth",
			{
				.resolution = { m_RenderContext.WindowWidth(), m_RenderContext.WindowHeight(), 1 },
				.format = VK_FORMAT_D32_SFLOAT,
				.imageUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
			});
	m_pRenderTarget = new RenderTarget(m_RenderContext);
	m_pRenderTarget->AttachTexture(eAttachmentPoint::Color0, pAttachment0)
		            .AttachTexture(eAttachmentPoint::DepthStencil, pAttachmentDepth).Build();

	auto hVS = rm.Create< Shader >(L"SimpleModelVS", Shader::CreationInfo{ .filepath = SPIRV_PATH.string() + "SimpleModel.vert.spv" });
	auto hFS = rm.Create< Shader >(L"SimpleModelPS", Shader::CreationInfo{ .filepath = SPIRV_PATH.string() + "SimpleModel.frag.spv" });
	m_pGraphicsPipeline = new GraphicsPipeline(m_RenderContext, "ForwardPSO");
	m_pGraphicsPipeline->SetShaders(hVS, hFS).SetRenderTarget(*m_pRenderTarget).SetDepthWriteEnable(true).Build();

	m_RenderContext.SetMainRenderPass(m_pRenderTarget->vkRenderPass());
}

ForwardModule::~ForwardModule()
{
	RELEASE(m_pGraphicsPipeline);
	RELEASE(m_pRenderTarget);
}

void ForwardModule::Apply(CommandBuffer& cmdBuffer)
{
	cmdBuffer.BeginRenderPass(*m_pRenderTarget);
	cmdBuffer.SetRenderPipeline(m_pGraphicsPipeline);

	cmdBuffer.DrawIndexedIndirect(*g_FrameData.pSceneResource);

	m_pRenderTarget->InvalidateImageLayout();

	g_FrameData.color = m_pRenderTarget->Attachment(eAttachmentPoint::Color0);
	g_FrameData.depth = m_pRenderTarget->Attachment(eAttachmentPoint::DepthStencil);
}

void ForwardModule::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pRenderTarget)
		m_pRenderTarget->Resize(width, height, depth);
}

} // namespace vk
