#include "RendererPch.h"
#include "VkRenderTarget.h"
#include "VkTexture.h"
#include "RenderDevice/VkCommandBuffer.h"
#include "RenderDevice/VkResourceManager.h"

namespace vk
{

RenderTarget::RenderTarget(RenderContext& context)
	: m_RenderContext(context)
	, m_Attachments(eAttachmentPoint::NumAttachmentPoints)
{
}

RenderTarget::~RenderTarget()
{
	if (m_vkFramebuffer) vkDestroyFramebuffer(m_RenderContext.vkDevice(), m_vkFramebuffer, nullptr);
	if (m_vkRenderPass) vkDestroyRenderPass(m_RenderContext.vkDevice(), m_vkRenderPass, nullptr);
}

RenderTarget& RenderTarget::AttachTexture(eAttachmentPoint attachmentPoint, baamboo::ResourceHandle< Texture > tex)
{
	assert(attachmentPoint < eAttachmentPoint::NumAttachmentPoints);
	m_Attachments[attachmentPoint] = tex;

    return *this;
}

RenderTarget& RenderTarget::SetLoadAttachment(eAttachmentPoint attachmentPoint)
{
	m_bLoadAttachmentBits |= (1 << attachmentPoint);
    return *this;
}

void RenderTarget::Build()
{
	const auto& rm = m_RenderContext.GetResourceManager();
	const bool bUseDepth = m_Attachments[eAttachmentPoint::DepthStencil].IsValid();

	// **
	// Render pass
	// **
	u32 width = 0; u32 height = 0; u32 depth = 0;
	std::vector< VkImageView > attachments; attachments.reserve(m_Attachments.size());
	std::vector< VkAttachmentReference > colorReferences; colorReferences.reserve(m_Attachments.size());
	std::vector< VkAttachmentDescription > attachmentDescs; attachmentDescs.reserve(m_Attachments.size());
	for (u32 i = 0; i < eAttachmentPoint::NumColorAttachments; ++i)
	{
		auto pTex = rm.Get(m_Attachments[i]);
		if (!pTex)
			continue;

		VkAttachmentDescription attachmentDesc = {};
		attachmentDesc.format = pTex->Desc().format;
		attachmentDesc.samples = pTex->Desc().samples;
		// VK_ATTACHMENT_LOAD_OP_DONT_CARE: appropriate if all pixels are sure to be replaced. since it is cost-effective than clear op. but remains clear for safety.
		attachmentDesc.loadOp = (m_bLoadAttachmentBits & i) ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDesc.finalLayout = 
			pTex->Desc().usage & VK_IMAGE_USAGE_SAMPLED_BIT ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		attachmentDescs.push_back(attachmentDesc);

		colorReferences.push_back(
			{
				.attachment = i,
				.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			});

		attachments.push_back(pTex->vkView());

		// resolution of all targets should be equal
		assert(width == 0 || width == pTex->Desc().extent.width);    width = pTex->Desc().extent.width;
		assert(height == 0 || height == pTex->Desc().extent.height); height = pTex->Desc().extent.height;
		assert(depth == 0 || depth == pTex->Desc().extent.depth);    depth = pTex->Desc().extent.depth;
	}
	m_NumColors = static_cast<u32>(attachments.size());

	VkAttachmentReference depthReference;
	if (bUseDepth)
	{
		auto pTex = rm.Get(m_Attachments[eAttachmentPoint::DepthStencil]);
		if (pTex)
		{
			const bool bIsDepthOnly = IsDepthOnly();

			VkAttachmentDescription attachmentDesc = {};
			attachmentDesc.format = pTex->Desc().format;
			attachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			//attachmentDesc.loadOp = bIsDepthOnly ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
			attachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			//attachmentDesc.initialLayout = bIsDepthOnly ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			attachmentDesc.finalLayout = bIsDepthOnly ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			attachmentDescs.push_back(attachmentDesc);

			depthReference.attachment = static_cast<u32>(attachmentDescs.size()) - 1;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			attachments.push_back(pTex->vkView());

			// resolution of all targets should be equal
			assert(width == 0  || width == pTex->Desc().extent.width);   width = pTex->Desc().extent.width;
			assert(height == 0 || height == pTex->Desc().extent.height); height = pTex->Desc().extent.height;
			assert(depth == 0  || depth == pTex->Desc().extent.depth);   depth = pTex->Desc().extent.depth;
		}
	}

	std::vector< VkSubpassDescription > subpasses;
	VkSubpassDescription subpassDesc = {};
	subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDesc.colorAttachmentCount = static_cast<u32>(colorReferences.size());
	subpassDesc.pColorAttachments = colorReferences.data();
	subpassDesc.pDepthStencilAttachment = bUseDepth ? &depthReference : nullptr;
	subpasses.push_back(subpassDesc);

	std::vector< VkSubpassDependency > dependencies;
	VkSubpassDependency subpassDependency = {};
	subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependency.dstSubpass = 0;
	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask = 0;
	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	dependencies.push_back(subpassDependency);

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<u32>(attachmentDescs.size());
	renderPassInfo.pAttachments = attachmentDescs.data();
	renderPassInfo.subpassCount = static_cast<u32>(subpasses.size());
	renderPassInfo.pSubpasses = subpasses.data();
	renderPassInfo.dependencyCount = static_cast<u32>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();
	VK_CHECK(vkCreateRenderPass(m_RenderContext.vkDevice(), &renderPassInfo, nullptr, &m_vkRenderPass));


	// **
	// Framebuffer
	// **
	VkFramebufferCreateInfo framebufferInfo = {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = m_vkRenderPass;
	framebufferInfo.attachmentCount = static_cast<u32>(attachments.size());
	framebufferInfo.pAttachments = attachments.data();
	framebufferInfo.width = width;
	framebufferInfo.height = height;
	framebufferInfo.layers = depth;
	VK_CHECK(vkCreateFramebuffer(m_RenderContext.vkDevice(), &framebufferInfo, nullptr, &m_vkFramebuffer));


	// **
	// Begin info
	// **
	auto pColor0 = rm.Get(m_Attachments[eAttachmentPoint::Color0]);
	auto pDepthStencil = rm.Get(m_Attachments[eAttachmentPoint::DepthStencil]);
	assert(pColor0 || pDepthStencil);

	for (u32 i = 0; i < eAttachmentPoint::NumAttachmentPoints; ++i)
	{
		auto pTex = rm.Get(m_Attachments[i]);
		if (!pTex)
			continue;

		m_ClearValues.push_back(pTex->ClearValue());
	}

	m_BeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	m_BeginInfo.renderPass = m_vkRenderPass;
	m_BeginInfo.framebuffer = m_vkFramebuffer;
	m_BeginInfo.renderArea = { { 0, 0 }, { width, height } };
	m_BeginInfo.clearValueCount = static_cast<u32>(m_ClearValues.size());
	m_BeginInfo.pClearValues = m_ClearValues.data();
}

void RenderTarget::Resize(u32 width, u32 height, u32 depth)
{
	const auto& rm = m_RenderContext.GetResourceManager();
	for (auto attachment : m_Attachments)
	{
		auto pAttachment = rm.Get(attachment);
		if (pAttachment)
			pAttachment->Resize(width, height, depth);
	}

	u32 bLoadAttachmentBits = m_bLoadAttachmentBits;
	std::vector< baamboo::ResourceHandle< Texture > > attachments = m_Attachments;
	Reset();

	m_Attachments = attachments;
	m_bLoadAttachmentBits = bLoadAttachmentBits;
	Build();
}

void RenderTarget::Reset()
{
	m_bLoadAttachmentBits = 0;

	m_Attachments.clear();
	m_Attachments.resize(eAttachmentPoint::NumAttachmentPoints);

	if (m_vkFramebuffer) vkDestroyFramebuffer(m_RenderContext.vkDevice(), m_vkFramebuffer, nullptr);
	if (m_vkRenderPass) vkDestroyRenderPass(m_RenderContext.vkDevice(), m_vkRenderPass, nullptr);
}

void RenderTarget::InvalidateImageLayout()
{
	for (u32 i = 0; i < eAttachmentPoint::NumColorAttachments; ++i)
	{
		auto pTex = m_RenderContext.GetResourceManager().Get(m_Attachments[i]);
		if (!pTex)
			continue;

		if (i == eAttachmentPoint::DepthStencil)
		{
			pTex->SetState(
				IsDepthOnly() ?
				Texture::State
					{
						.access = VK_ACCESS_SHADER_READ_BIT,
						.stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					} :
				Texture::State
					{
						.access = VK_ACCESS_SHADER_READ_BIT,
						.stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
					});
		}
		else
		{
			pTex->SetState(
				pTex->Desc().usage & VK_IMAGE_USAGE_SAMPLED_BIT ?
				Texture::State
				{
					.access = VK_ACCESS_SHADER_READ_BIT,
					.stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				} :
				Texture::State
				{
					.access = VK_ACCESS_TRANSFER_READ_BIT,
					.stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
					.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
				});
		}
	}
}

VkViewport RenderTarget::GetViewport(float2 scale, float2 bias, f32 minDepth, f32 maxDepth) const
{
	const auto& rm = m_RenderContext.GetResourceManager();

	auto pColor0 = rm.Get(m_Attachments[eAttachmentPoint::Color0]);
	auto pDepthStencil = rm.Get(m_Attachments[eAttachmentPoint::DepthStencil]);
	assert(pColor0 || pDepthStencil);

	u32 width = pColor0 ?
		pColor0->Desc().extent.width : pDepthStencil->Desc().extent.width;
	u32 height = pColor0 ?
		pColor0->Desc().extent.height : pDepthStencil->Desc().extent.height;

	VkViewport viewport = {};
	viewport.x = static_cast<float>(width) * bias.x;
	viewport.y = static_cast<float>(height) * bias.y;
	viewport.width = static_cast<float>(width) * scale.x;
	viewport.height = static_cast<float>(height) * scale.y;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;

	return viewport;
}

VkRect2D RenderTarget::GetScissorRect() const
{
	const auto& rm = m_RenderContext.GetResourceManager();

	auto pColor0 = rm.Get(m_Attachments[eAttachmentPoint::Color0]);
	auto pDepthStencil = rm.Get(m_Attachments[eAttachmentPoint::DepthStencil]);
	assert(pColor0 || pDepthStencil);

	u32 width = pColor0 ?
		pColor0->Desc().extent.width : pDepthStencil->Desc().extent.width;
	u32 height = pColor0 ?
		pColor0->Desc().extent.height : pDepthStencil->Desc().extent.height;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = { width, height };

	return scissor;
}

bool RenderTarget::IsDepthOnly() const
{
    bool bDepthOnly = true;
    for (u32 i = 0; i < eAttachmentPoint::NumColorAttachments; ++i)
        if (m_Attachments[i].IsValid()) bDepthOnly = false;

    assert(!bDepthOnly || m_Attachments[eAttachmentPoint::DepthStencil].IsValid());
    return bDepthOnly;
}

} // namespace vk