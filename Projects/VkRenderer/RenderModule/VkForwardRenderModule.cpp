#include "RendererPch.h"
#include "VkForwardRenderModule.h"
#include "RenderDevice/VkRenderContext.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandBuffer.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkRenderTarget.h"

namespace vk
{

namespace
{

RenderTarget*     _pRenderTarget = nullptr;
GraphicsPipeline* _pGraphicsPipeline = nullptr;

}

void ForwardPass::Initialize(RenderContext& renderContext)
{
	auto& rm = renderContext.GetResourceManager();
	auto pAttachment0 = 
		rm.Create< Texture >(
			"ForwardPass::Attachment0", 
			{
				.resolution = { renderContext.ViewportWidth(), renderContext.ViewportHeight(), 1 },
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			});
	_pRenderTarget = new RenderTarget(renderContext);
	_pRenderTarget->AttachTexture(eAttachmentPoint::Color0, pAttachment0).Build();

	auto hVS = rm.Create< Shader >("SimpleTriangleVS", Shader::CreationInfo{ .filepath = SPIRV_PATH + "SimpleTriangle.vert.spv" });
	auto hFS = rm.Create< Shader >("SimpleTrianglePS", Shader::CreationInfo{ .filepath = SPIRV_PATH + "SimpleTriangle.frag.spv" });
	_pGraphicsPipeline = new GraphicsPipeline(renderContext, "ForwardPSO");
	_pGraphicsPipeline->SetShaders(hVS, hFS).SetRenderTarget(*_pRenderTarget).Build();

	renderContext.SetMainRenderPass(_pRenderTarget->vkRenderPass());
}

void ForwardPass::Apply(CommandBuffer& cmdBuffer)
{
	cmdBuffer.BeginRenderPass(*_pRenderTarget);
	cmdBuffer.SetRenderPipeline(_pGraphicsPipeline);

	cmdBuffer.Draw(3);

	_pRenderTarget->InvalidateImageLayout();
}

void ForwardPass::Destroy()
{
	RELEASE(_pGraphicsPipeline);
	RELEASE(_pRenderTarget);
}

void ForwardPass::Resize(u32 width, u32 height, u32 depth)
{
	if (_pRenderTarget)
		_pRenderTarget->Resize(width, height, depth);
}

baamboo::ResourceHandle< Texture > ForwardPass::GetRenderedTexture(eAttachmentPoint attachment)
{
	return _pRenderTarget->Attachment(attachment);
}

} // namespace vk
