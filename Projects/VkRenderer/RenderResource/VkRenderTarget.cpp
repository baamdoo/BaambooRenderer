#include "RendererPch.h"
#include "VkRenderTarget.h"
#include "VkTexture.h"

namespace vk
{

VulkanRenderTarget::VulkanRenderTarget(VkRenderDevice& rd, const char* name)
	: render::RenderTarget(name)
	, m_RenderDevice(rd)
{
}

VulkanRenderTarget::~VulkanRenderTarget()
{
	DestroyNativeObjects();
}

VulkanRenderTarget& VulkanRenderTarget::AttachTexture(render::eAttachmentPoint attachmentPoint, Arc< render::Texture > tex)
{
	using namespace render;

	assert(attachmentPoint < eAttachmentPoint::NumAttachmentPoints);
	m_pAttachments[attachmentPoint] = tex;

    return *this;
}

void VulkanRenderTarget::Build()
{
	using namespace render;

	const bool bUseDepth = m_pAttachments[eAttachmentPoint::DepthStencil] != nullptr;

	// **
	// Render pass
	// **
	u32 width = 0; u32 height = 0; u32 depth = 0;
	VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
	bool bHasAttachment = false;
	const auto validateSampleCount = [&](const VulkanTexture& texture)
	{
		if (!bHasAttachment)
		{
			sampleCount = texture.Desc().samples;
			bHasAttachment = true;
			return;
		}

		BB_ASSERT(sampleCount == texture.Desc().samples,
			"Vulkan render target attachments must use the same sample count.");
	};

	std::vector< VkImageView > attachments; attachments.reserve(m_pAttachments.size());
	std::vector< VkAttachmentReference > colorReferences(
		eAttachmentPoint::NumColorAttachments,
		{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
	std::vector< VkAttachmentDescription > attachmentDescs; attachmentDescs.reserve(m_pAttachments.size());
	u32 numColorSlots = 0;
	for (u32 i = 0; i < eAttachmentPoint::NumColorAttachments; ++i)
	{
		auto tex = StaticCast<VulkanTexture>(m_pAttachments[i]);
		if (!tex)
			continue;

		bool bLoad = (m_bLoadAttachmentBits & (1 << i)) != 0;
		validateSampleCount(*tex);
		const u32 attachmentIndex = static_cast<u32>(attachmentDescs.size());
		numColorSlots = i + 1;

		VkAttachmentDescription attachmentDesc = {};
		attachmentDesc.format         = tex->Desc().format;
		attachmentDesc.samples        = tex->Desc().samples;
		attachmentDesc.loadOp         = bLoad ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDesc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDesc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDesc.finalLayout    =
			tex->Desc().usage & VK_IMAGE_USAGE_SAMPLED_BIT ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		attachmentDesc.initialLayout  = bLoad ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescs.push_back(attachmentDesc);

		colorReferences[i].attachment = attachmentIndex;
		colorReferences[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		attachments.push_back(tex->vkView());

		// resolution of all targets should be equal
		assert(width  == 0 || width  == tex->Desc().extent.width);    width = tex->Desc().extent.width;
		assert(height == 0 || height == tex->Desc().extent.height);  height = tex->Desc().extent.height;
		assert(depth  == 0 || depth  == tex->Desc().extent.depth);    depth = tex->Desc().extent.depth;
	}
	colorReferences.resize(numColorSlots);
	m_NumColors = numColorSlots;

	VkAttachmentReference depthReference = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
	if (bUseDepth)
	{
		auto pTex = StaticCast<VulkanTexture>(m_pAttachments[eAttachmentPoint::DepthStencil]);
		if (pTex)
		{
			bool bLoadDepth = (m_bLoadAttachmentBits & (1 << eAttachmentPoint::DepthStencil)) != 0;
			validateSampleCount(*pTex);

			VkAttachmentDescription attachmentDesc = {};
			attachmentDesc.format         = pTex->Desc().format;
			attachmentDesc.samples        = pTex->Desc().samples;
			attachmentDesc.loadOp         = bLoadDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDesc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDesc.stencilLoadOp  = bLoadDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDesc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			attachmentDesc.initialLayout  = bLoadDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
			attachmentDescs.push_back(attachmentDesc);

			depthReference.attachment = static_cast<u32>(attachmentDescs.size()) - 1;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			attachments.push_back(pTex->vkView());

			// resolution of all targets should be equal
			assert(width  == 0  || width  == pTex->Desc().extent.width);   width  = pTex->Desc().extent.width;
			assert(height == 0  || height == pTex->Desc().extent.height);  height = pTex->Desc().extent.height;
			assert(depth  == 0  || depth  == pTex->Desc().extent.depth);   depth  = pTex->Desc().extent.depth;
		}
	}

	std::vector< VkSubpassDescription > subpasses;
	VkSubpassDescription subpassDesc = {};
	subpassDesc.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDesc.colorAttachmentCount    = static_cast<u32>(colorReferences.size());
	subpassDesc.pColorAttachments       = colorReferences.data();
	subpassDesc.pDepthStencilAttachment = bUseDepth ? &depthReference : nullptr;
	subpasses.push_back(subpassDesc);

	std::vector< VkSubpassDependency > dependencies;
	VkSubpassDependency subpassDependency = {};
	subpassDependency.srcSubpass      = VK_SUBPASS_EXTERNAL;
	subpassDependency.dstSubpass      = 0;
	subpassDependency.srcStageMask    = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	subpassDependency.dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask   = 0;
	// READ is required for loadOp=LOAD and color blending; WRITE for normal color output and loadOp=CLEAR.
	subpassDependency.dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	subpassDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	if (bUseDepth)
	{
		// Cover the UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL layout transition
		// and the loadOp=CLEAR on the depth aspect, both of which execute at
		// EARLY/LATE_FRAGMENT_TESTS with DEPTH_STENCIL_ATTACHMENT_WRITE access.
		// READ is required for loadOp=LOAD and depth-test reads.
		subpassDependency.dstStageMask  |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		subpassDependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	}
	dependencies.push_back(subpassDependency);

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<u32>(attachmentDescs.size());
	renderPassInfo.pAttachments    = attachmentDescs.data();
	renderPassInfo.subpassCount    = static_cast<u32>(subpasses.size());
	renderPassInfo.pSubpasses      = subpasses.data();
	renderPassInfo.dependencyCount = static_cast<u32>(dependencies.size());
	renderPassInfo.pDependencies   = dependencies.data();
	VK_CHECK(vkCreateRenderPass(m_RenderDevice.vkDevice(), &renderPassInfo, nullptr, &m_vkRenderPass));


	// **
	// Framebuffer
	// **
	VkFramebufferCreateInfo framebufferInfo = {};
	framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass      = m_vkRenderPass;
	framebufferInfo.attachmentCount = static_cast<u32>(attachments.size());
	framebufferInfo.pAttachments    = attachments.data();
	framebufferInfo.width           = width;
	framebufferInfo.height          = height;
	framebufferInfo.layers          = depth;
	VK_CHECK(vkCreateFramebuffer(m_RenderDevice.vkDevice(), &framebufferInfo, nullptr, &m_vkFramebuffer));


	// **
	// Begin info
	// **
	BB_ASSERT(!attachments.empty(),
		"Vulkan render target requires at least one color or depth attachment.");

	for (u32 i = 0; i < eAttachmentPoint::NumAttachmentPoints; ++i)
	{
		auto pTex = StaticCast<VulkanTexture>(m_pAttachments[i]);
		if (!pTex)
			continue;

		m_ClearValues.push_back(pTex->ClearValue());
	}

	m_BeginInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	m_BeginInfo.renderPass      = m_vkRenderPass;
	m_BeginInfo.framebuffer     = m_vkFramebuffer;
	m_BeginInfo.renderArea      = { { 0, 0 }, { width, height } };
	m_BeginInfo.clearValueCount = static_cast<u32>(m_ClearValues.size());
	m_BeginInfo.pClearValues    = m_ClearValues.data();

	m_RenderDevice.SetVkObjectName(m_Name, (u64)m_vkRenderPass, VK_OBJECT_TYPE_RENDER_PASS);
	m_RenderDevice.SetVkObjectName(m_Name, (u64)m_vkFramebuffer, VK_OBJECT_TYPE_FRAMEBUFFER);
}

void VulkanRenderTarget::DestroyNativeObjects()
{
	if (m_vkFramebuffer)
	{
		vkDestroyFramebuffer(m_RenderDevice.vkDevice(), m_vkFramebuffer, nullptr);
		m_vkFramebuffer = VK_NULL_HANDLE;
	}
	if (m_vkRenderPass)
	{
		vkDestroyRenderPass(m_RenderDevice.vkDevice(), m_vkRenderPass, nullptr);
		m_vkRenderPass = VK_NULL_HANDLE;
	}

	m_BeginInfo = {};
	m_ClearValues.clear();
}

void VulkanRenderTarget::Resize(u32 width, u32 height, u32 depth)
{
	DestroyNativeObjects();

	for (const auto& attachment : m_pAttachments)
		if (attachment)
			attachment->Resize(width, height, depth);

	Build();
}

void VulkanRenderTarget::Reset()
{
	using namespace render;

	DestroyNativeObjects();
	m_bLoadAttachmentBits = 0;
	m_pAttachments.assign(eAttachmentPoint::NumAttachmentPoints, nullptr);
	m_NumColors = 0;
}

void VulkanRenderTarget::InvalidateImageLayout()
{
	using namespace render;

	for (u32 i = 0; i < eAttachmentPoint::NumAttachmentPoints; ++i)
	{
		auto pTex = StaticCast<VulkanTexture>(m_pAttachments[i]);
		if (!pTex)
			continue;

		if (i == eAttachmentPoint::DepthStencil)
		{
			pTex->SetState(BarrierStates::PixelShaderReadOnly);
		}
		else
		{
			pTex->SetState(
				pTex->Desc().usage & VK_IMAGE_USAGE_SAMPLED_BIT ? BarrierStates::PixelShaderReadOnly : BarrierStates::TransferSource
			);
		}
	}
}

VkViewport VulkanRenderTarget::GetViewport(float2 scale, float2 bias, f32 minDepth, f32 maxDepth) const
{
	const u32 width  = m_BeginInfo.renderArea.extent.width;
	const u32 height = m_BeginInfo.renderArea.extent.height;
	BB_ASSERT(width > 0 && height > 0,
		"Vulkan render target must be built before querying its viewport.");

	VkViewport viewport = {};
	viewport.x        = static_cast<float>(width) * bias.x;
	viewport.y        = static_cast<float>(height) * bias.y;
	viewport.width    = static_cast<float>(width) * scale.x;
	viewport.height   = static_cast<float>(height) * scale.y;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;

	return viewport;
}

VkRect2D VulkanRenderTarget::GetScissorRect() const
{
	const u32 width  = m_BeginInfo.renderArea.extent.width;
	const u32 height = m_BeginInfo.renderArea.extent.height;
	BB_ASSERT(width > 0 && height > 0,
		"Vulkan render target must be built before querying its scissor rectangle.");

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = { width, height };

	return scissor;
}

bool VulkanRenderTarget::IsDepthOnly() const
{
	using namespace render;

    bool bDepthOnly = true;
    for (u32 i = 0; i < eAttachmentPoint::NumColorAttachments; ++i)
        if (m_pAttachments[i]) 
			bDepthOnly = false;

    assert(!bDepthOnly || m_pAttachments[eAttachmentPoint::DepthStencil]);
    return bDepthOnly;
}

} // namespace vk
