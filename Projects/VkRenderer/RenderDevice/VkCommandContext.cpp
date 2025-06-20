#include "RendererPch.h"
#include "VkCommandContext.h"
#include "VkCommandQueue.h"
#include "VkRenderPipeline.h"
#include "VkBufferAllocator.h"
#include "VkResourceManager.h"
#include "VkDescriptorSet.h"
#include "RenderResource/VkBuffer.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSceneResource.h"

namespace vk
{

static PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR;

CommandContext::CommandContext(RenderDevice& device, VkCommandPool vkCommandPool, eCommandType type, VkCommandBufferLevel level)
	: m_RenderDevice(device)
	, m_CommandType(type)
    , m_vkBelongedPool(vkCommandPool)
    , m_Level(level)
{
	vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)vkGetInstanceProcAddr(m_RenderDevice.vkInstance(), "vkCmdPushDescriptorSetKHR");

    // **
    // Allocate command buffer
    // **
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_vkBelongedPool;
    allocInfo.level = m_Level;
    allocInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(m_RenderDevice.vkDevice(), &allocInfo, &m_vkCommandBuffer));


	// **
	// Create dynamic buffer pools
	// **
	m_pUniformBufferPool = new DynamicBufferAllocator(m_RenderDevice);


    // **
    // Create sync-objects
    // **
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(m_RenderDevice.vkDevice(), &fenceInfo, nullptr, &m_vkRenderCompleteFence);
    vkCreateFence(m_RenderDevice.vkDevice(), &fenceInfo, nullptr, &m_vkPresentCompleteFence);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(m_RenderDevice.vkDevice(), &semaphoreInfo, nullptr, &m_vkRenderCompleteSemaphore));
    VK_CHECK(vkCreateSemaphore(m_RenderDevice.vkDevice(), &semaphoreInfo, nullptr, &m_vkPresentCompleteSemaphore));
}

CommandContext::~CommandContext()
{
	RELEASE(m_pUniformBufferPool);

    vkDestroySemaphore(m_RenderDevice.vkDevice(), m_vkPresentCompleteSemaphore, nullptr);
    vkDestroySemaphore(m_RenderDevice.vkDevice(), m_vkRenderCompleteSemaphore, nullptr);
    vkDestroyFence(m_RenderDevice.vkDevice(), m_vkPresentCompleteFence, nullptr);
    vkDestroyFence(m_RenderDevice.vkDevice(), m_vkRenderCompleteFence, nullptr);

    vkFreeCommandBuffers(m_RenderDevice.vkDevice(), m_vkBelongedPool, 1, &m_vkCommandBuffer);
}

void CommandContext::Open(VkCommandBufferUsageFlags flags)
{
    m_CurrentContextIndex = m_RenderDevice.ContextIndex();

	VkFence vkFences[2] = {m_vkRenderCompleteFence, m_vkPresentCompleteFence};
    VK_CHECK(vkResetFences(m_RenderDevice.vkDevice(), 2, vkFences));
    VK_CHECK(vkResetCommandBuffer(m_vkCommandBuffer, 0));

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;
    VK_CHECK(vkBeginCommandBuffer(m_vkCommandBuffer, &beginInfo));

	m_pUniformBufferPool->Reset();
	m_PushAllocations.clear();

	m_pGraphicsPipeline = nullptr;
	m_pComputePipeline = nullptr;
}

void CommandContext::Close()
{
    FlushBarriers();
    VK_CHECK(vkEndCommandBuffer(m_vkCommandBuffer));
}

void CommandContext::Execute()
{
	switch (m_CommandType)
	{
	case eCommandType::Graphics:
		m_RenderDevice.GraphicsQueue().ExecuteCommandBuffer(*this);
		break;
	case eCommandType::Compute:
		m_RenderDevice.ComputeQueue().ExecuteCommandBuffer(*this);
		break;
	case eCommandType::Transfer:
		m_RenderDevice.TransferQueue().ExecuteCommandBuffer(*this);
		break;
	}
}

bool CommandContext::IsReady() const
{
	return IsFenceComplete(m_vkRenderCompleteFence) && IsFenceComplete(m_vkPresentCompleteFence);
}

bool CommandContext::IsFenceComplete(VkFence vkFence) const
{
    return vkGetFenceStatus(m_RenderDevice.vkDevice(), vkFence) == VK_SUCCESS;
}

void CommandContext::WaitForFence(VkFence vkFence) const
{
	VK_CHECK(vkWaitForFences(m_RenderDevice.vkDevice(), 1, &vkFence, VK_TRUE, UINT64_MAX));
}

void CommandContext::Flush() const
{
	WaitForFence(m_vkRenderCompleteFence);
	WaitForFence(m_vkPresentCompleteFence);
}

void CommandContext::CopyBuffer(
	VkBuffer vkDstBuffer, 
	VkBuffer vkSrcBuffer, 
	VkDeviceSize sizeInBytes, 
	VkPipelineStageFlags2 dstStageMask,
	VkDeviceSize dstOffset, 
	VkDeviceSize srcOffset, 
	bool bFlushImmediate)
{
	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = srcOffset;
	copyRegion.dstOffset = dstOffset;
	copyRegion.size = sizeInBytes;
	vkCmdCopyBuffer(m_vkCommandBuffer, vkSrcBuffer, vkDstBuffer, 1, &copyRegion);

	if (!m_bTransient)
	{
		VkBufferMemoryBarrier2 copyBarrier = {};
		copyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		copyBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		copyBarrier.dstStageMask = dstStageMask;
		copyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		copyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarrier.buffer = vkDstBuffer;
		copyBarrier.offset = 0;
		copyBarrier.size = sizeInBytes;
		AddBarrier(copyBarrier, bFlushImmediate);
	}
}

void CommandContext::CopyBuffer(
	Arc< Buffer > pDstBuffer,
	Arc< Buffer > pSrcBuffer,
	VkDeviceSize sizeInBytes,
	VkPipelineStageFlags2 dstStageMask, 
	VkDeviceSize dstOffset, 
	VkDeviceSize srcOffset, 
	bool bFlushImmediate)
{
	CopyBuffer(
		pDstBuffer->vkBuffer(), 
		pSrcBuffer->vkBuffer(), 
		sizeInBytes,
		dstStageMask, 
		dstOffset, 
		srcOffset, 
		bFlushImmediate
	);
}

void CommandContext::CopyBuffer(Arc< Texture > pDstTexture, Arc< Buffer > pSrcBuffer, const std::vector< VkBufferImageCopy >& regions, bool bAllSubresources)
{
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = bAllSubresources ? VK_REMAINING_MIP_LEVELS : pDstTexture->Desc().mipLevels;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = bAllSubresources ? VK_REMAINING_ARRAY_LAYERS : pDstTexture->Desc().arrayLayers;
    
    TransitionImageLayout(
        pDstTexture,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        subresourceRange);

    vkCmdCopyBufferToImage(m_vkCommandBuffer, pSrcBuffer->vkBuffer(), pDstTexture->vkImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<u32>(regions.size()), regions.data());

    TransitionImageLayout(
        pDstTexture, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        subresourceRange);
}

void CommandContext::CopyTexture(Arc< Texture > pDstTexture, Arc< Texture > pSrcTexture)
{
	TransitionImageLayout(
		pSrcTexture, 
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
		//VK_PIPELINE_STAGE_2_HOST_BIT, 
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, 
		pSrcTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, true);
	TransitionImageLayout(
		pDstTexture,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		//VK_PIPELINE_STAGE_2_HOST_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		pSrcTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, true);

	VkImageCopy copyRegion{};
	copyRegion.srcSubresource.aspectMask = pSrcTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
	copyRegion.srcSubresource.mipLevel = 0;
	copyRegion.srcSubresource.baseArrayLayer = 0;
	copyRegion.srcSubresource.layerCount = 1;
	copyRegion.srcOffset = { 0, 0, 0 };

	copyRegion.dstSubresource.aspectMask = pDstTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
	copyRegion.dstSubresource.mipLevel = 0;
	copyRegion.dstSubresource.baseArrayLayer = 0;
	copyRegion.dstSubresource.layerCount = 1;
	copyRegion.dstOffset = { 0, 0, 0 };

	copyRegion.extent = pSrcTexture->Desc().extent;

	vkCmdCopyImage(m_vkCommandBuffer, pSrcTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pDstTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
}

void CommandContext::BlitTexture(Arc<Texture> pDstTexture, Arc<Texture> pSrcTexture)
{
	VkImageAspectFlags format = 
		pSrcTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
	TransitionImageLayout(
		pSrcTexture,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		//VK_PIPELINE_STAGE_2_HOST_BIT, 
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		format, true);
	TransitionImageLayout(
		pDstTexture,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		//VK_PIPELINE_STAGE_2_HOST_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		format, true);

	const auto& src = pSrcTexture->Desc().extent;
	const auto& dst = pDstTexture->Desc().extent;
	VkImageBlit blitRegion = {};
	blitRegion.srcSubresource = { format, 0, 0, 1 };
	blitRegion.srcOffsets[0]  = { 0, 0, 0 };
	blitRegion.srcOffsets[1]  = { static_cast<i32>(src.width), static_cast<i32>(src.height), static_cast<i32>(src.depth) };
	blitRegion.dstSubresource = { format, 0, 0, 1 };
	blitRegion.dstOffsets[0]  = { 0, 0, 0 };
	blitRegion.dstOffsets[1]  = { static_cast<i32>(dst.width), static_cast<i32>(dst.height), static_cast<i32>(dst.depth) };

	vkCmdBlitImage(m_vkCommandBuffer,
		pSrcTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		pDstTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, 
		&blitRegion,
		VK_FILTER_LINEAR);
}

void CommandContext::GenerateMips(Arc< Texture > pTexture)
{
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount = 1;

	const auto& desc = pTexture->Desc();
	for (u32 level = 0; level < desc.mipLevels - 1; ++level)
	{
		i32 w = desc.extent.width >> level;
		i32 h = desc.extent.height >> level;

		subresourceRange.baseMipLevel = level;
		TransitionImageLayout(
			pTexture,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			//VK_PIPELINE_STAGE_TRANSFER_BIT, 
			VK_PIPELINE_STAGE_TRANSFER_BIT, 
			subresourceRange);

		VkImageBlit blit = {};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { w, h, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = level;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { w > 1 ? w / 2 : 1, h > 1 ? h / 2 : 1, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = level + 1;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		vkCmdBlitImage(m_vkCommandBuffer,
			pTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			pTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);

		TransitionImageLayout(
			pTexture,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			//VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			subresourceRange);
	}

	subresourceRange.baseMipLevel = desc.mipLevels - 1;
	TransitionImageLayout(
		pTexture,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		//VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		subresourceRange);
}

void CommandContext::TransitionImageLayout(
	Arc< Texture > pTexture,
	VkImageLayout newLayout, 
	VkPipelineStageFlags2 dstStageMask,
	VkImageAspectFlags aspectMask,
	bool bFlushImmediate, 
	bool bFlatten)
{
	TransitionImageLayout(
		pTexture, 
		newLayout, 
		dstStageMask, 
		{ aspectMask, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }, bFlushImmediate, bFlatten);
}

void CommandContext::TransitionImageLayout(
	Arc< Texture > pTexture,
    VkImageLayout newLayout, 
	VkPipelineStageFlags2 dstStageMask,
    VkImageSubresourceRange subresourceRange, 
	bool bFlushImmediate, 
	bool bFlatten)
{
	Texture::State oldState = pTexture->GetState().GetSubresourceState(subresourceRange);
	if (oldState.layout == newLayout)
	{
		return;
	}

	VkImageMemoryBarrier2 imageMemoryBarrier = {};
	imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.srcAccessMask = oldState.access;
	imageMemoryBarrier.dstAccessMask = 0;
	imageMemoryBarrier.srcStageMask = oldState.stage;
	imageMemoryBarrier.dstStageMask = dstStageMask;
	imageMemoryBarrier.oldLayout = oldState.layout;
	imageMemoryBarrier.newLayout = newLayout;
	imageMemoryBarrier.image = pTexture->vkImage();
	imageMemoryBarrier.subresourceRange = subresourceRange;

	// Destination access mask controls the dependency for the new image layout
	switch (newLayout)
	{
	case VK_IMAGE_LAYOUT_GENERAL:
		// Assume this layout is used for write to image only
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		// Make sure any writes to the image have been finished
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		// Make sure any reads from the image have been finished
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		// Make sure any writes to the color buffer have been finished
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Make sure any writes to depth/stencil buffer have been finished
		imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// Make sure any writes to the image have been finished
		if (imageMemoryBarrier.srcAccessMask == 0)
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		break;
	}

	Texture::State newState = { 
		.access = imageMemoryBarrier.dstAccessMask, 
		.stage = dstStageMask, 
		.layout = newLayout 
	};
	if (pTexture)
	{
		if (bFlatten)
		{
			pTexture->FlattenSubresourceStates();
		}

		const auto& stateBefore = pTexture->GetState();
		if (stateBefore.GetSubresourceState(subresourceRange) != newState)
		{
			AddBarrier(imageMemoryBarrier, bFlushImmediate);

			pTexture->SetState(newState, subresourceRange);
		}
	}
}

void CommandContext::ClearTexture(
	Arc< Texture > pTexture, 
	VkImageLayout newLayout, 
	VkPipelineStageFlags2 dstStageMask,
	u32 baseMip, u32 numMips, u32 baseArray, u32 numArrays)
{
	VkImageSubresourceRange range = {};
	range.aspectMask = pTexture->AspectMask();
	range.baseMipLevel = baseMip;
	range.levelCount = numMips;
	range.baseArrayLayer = baseArray;
	range.layerCount = numArrays;

	TransitionImageLayout(pTexture, newLayout, dstStageMask, range);
	if (pTexture->AspectMask() & VK_IMAGE_ASPECT_COLOR_BIT)
	{
		vkCmdClearColorImage(m_vkCommandBuffer, pTexture->vkImage(), pTexture->GetState().GetSubresourceState().layout, pTexture->ClearColorValue(), 1, &range);
	}
	else
	{
		vkCmdClearDepthStencilImage(m_vkCommandBuffer, pTexture->vkImage(), pTexture->GetState().GetSubresourceState().layout, pTexture->ClearDepthValue(), 1, &range);
	}
}

void CommandContext::SetGraphicsPushConstants(u32 sizeInBytes, void* data, VkShaderStageFlags stages, u32 offsetInBytes)
{
	assert(m_pGraphicsPipeline);
	vkCmdPushConstants(m_vkCommandBuffer, m_pGraphicsPipeline->vkPipelineLayout(), stages, offsetInBytes, sizeInBytes, data);
}

void CommandContext::SetGraphicsDynamicUniformBuffer(u32 binding, VkDeviceSize sizeInBytes, const void* bufferData)
{
	auto allocation = m_pUniformBufferPool->Allocate(sizeInBytes);
	memcpy(allocation.cpuHandle, bufferData, sizeInBytes);

	VkDescriptorBufferInfo bufferInfo = {};
	bufferInfo.buffer = allocation.vkBuffer;
	bufferInfo.offset = allocation.offset;
	bufferInfo.range  = allocation.size;

	VkWriteDescriptorSet write = {};
	write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet          = VK_NULL_HANDLE;
	write.dstBinding      = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	write.pBufferInfo     = &bufferInfo;
	
	m_PushAllocations.push_back({ binding, bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER });
}

void CommandContext::PushDescriptors(u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType)
{
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = VK_NULL_HANDLE;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = descriptorType;
	write.pImageInfo = &imageInfo;

	m_PushAllocations.push_back({ binding, imageInfo, descriptorType });
}

void CommandContext::PushDescriptors(u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType)
{
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = VK_NULL_HANDLE;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = descriptorType;
	write.pBufferInfo = &bufferInfo;

	m_PushAllocations.push_back({ binding, bufferInfo, descriptorType });
}

void CommandContext::SetRenderPipeline(GraphicsPipeline* pRenderPipeline)
{
	m_pComputePipeline = nullptr;
	if (pRenderPipeline && m_pGraphicsPipeline != pRenderPipeline)
	{
		m_pGraphicsPipeline = pRenderPipeline;
		vkCmdBindPipeline(m_vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pGraphicsPipeline->vkPipeline());

		m_PushAllocations.clear();
	}
}

void CommandContext::SetRenderPipeline(ComputePipeline* pRenderPipeline)
{
	m_pGraphicsPipeline = nullptr;
	if (pRenderPipeline && m_pComputePipeline != pRenderPipeline)
	{
		m_pComputePipeline = pRenderPipeline;
		vkCmdBindPipeline(m_vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pComputePipeline->vkPipeline());

		m_PushAllocations.clear();
	}
}

void CommandContext::BeginRenderPass(const RenderTarget& renderTarget)
{
	auto viewport = renderTarget.GetViewport();
	vkCmdSetViewport(m_vkCommandBuffer, 0, 1, &viewport);

	auto scissor = renderTarget.GetScissorRect();
	vkCmdSetScissor(m_vkCommandBuffer, 0, 1, &scissor);

	const auto& beginInfo = renderTarget.GetBeginInfo();
	vkCmdBeginRenderPass(m_vkCommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void CommandContext::EndRenderPass()
{
	vkCmdEndRenderPass(m_vkCommandBuffer);
}

void CommandContext::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
	FlushBarriers();

	std::vector< VkWriteDescriptorSet > writes;
	for (const auto& allocation : m_PushAllocations)
	{
		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = VK_NULL_HANDLE;
		write.dstBinding = allocation.binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = allocation.descriptorType;
		if (allocation.descriptor.bImage)
			write.pImageInfo = &allocation.descriptor.imageInfo;
		else
			write.pBufferInfo = &allocation.descriptor.bufferInfo;
		writes.push_back(write);		
	}

	vkCmdPushDescriptorSetKHR(
		m_vkCommandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_pGraphicsPipeline->vkPipelineLayout(),
		eDescriptorSet_Push, static_cast<u32>(writes.size()), writes.data());

	vkCmdDraw(m_vkCommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandContext::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
	FlushBarriers();

	std::vector< VkWriteDescriptorSet > writes;
	for (const auto& allocation : m_PushAllocations)
	{
		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = VK_NULL_HANDLE;
		write.dstBinding = allocation.binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = allocation.descriptorType;
		if (allocation.descriptor.bImage)
			write.pImageInfo = &allocation.descriptor.imageInfo;
		else
			write.pBufferInfo = &allocation.descriptor.bufferInfo;
		writes.push_back(write);
	}

	vkCmdPushDescriptorSetKHR(
		m_vkCommandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_pGraphicsPipeline->vkPipelineLayout(),
		eDescriptorSet_Push, static_cast<u32>(writes.size()), writes.data());

	vkCmdDrawIndexed(m_vkCommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void CommandContext::DrawIndexedIndirect(const SceneResource& sceneResource)
{
	FlushBarriers();

	auto vkDescriptorSet = sceneResource.GetSceneDescriptorSet();
	vkCmdBindDescriptorSets(
		m_vkCommandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_pGraphicsPipeline->vkPipelineLayout(),
		eDescriptorSet_Static, 1, &vkDescriptorSet, 0, nullptr);

	std::vector< VkWriteDescriptorSet > writes;
	for (const auto& allocation : m_PushAllocations)
	{
		VkWriteDescriptorSet write = {};
		write.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet               = VK_NULL_HANDLE;
		write.dstBinding           = allocation.binding;
		write.dstArrayElement      = 0;
		write.descriptorCount      = 1;
		write.descriptorType       = allocation.descriptorType;
		if (allocation.descriptor.bImage)
			write.pImageInfo       = &allocation.descriptor.imageInfo;
		else
			write.pBufferInfo      = &allocation.descriptor.bufferInfo;
		writes.push_back(write);
	}

	vkCmdPushDescriptorSetKHR(
		m_vkCommandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_pGraphicsPipeline->vkPipelineLayout(),
		eDescriptorSet_Push, static_cast<u32>(writes.size()), writes.data());

	const auto& indirectInfo = sceneResource.GetIndirectBufferInfo();
	vkCmdBindIndexBuffer(m_vkCommandBuffer, sceneResource.GetIndexBufferInfo().buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexedIndirect(m_vkCommandBuffer, indirectInfo.buffer, indirectInfo.offset, u32(indirectInfo.range / sizeof(IndirectDrawData)), sizeof(IndirectDrawData));
}

void CommandContext::Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	FlushBarriers();

	std::vector< VkWriteDescriptorSet > writes;
	for (const auto& allocation : m_PushAllocations)
	{
		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = VK_NULL_HANDLE;
		write.dstBinding = allocation.binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = allocation.descriptorType;
		if (allocation.descriptor.bImage)
		    write.pImageInfo = &allocation.descriptor.imageInfo;
		else
			write.pBufferInfo = &allocation.descriptor.bufferInfo;
		writes.push_back(write);
	}

	vkCmdPushDescriptorSetKHR(
		m_vkCommandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		m_pComputePipeline->vkPipelineLayout(),
		eDescriptorSet_Push, static_cast<u32>(writes.size()), writes.data());

	vkCmdDispatch(m_vkCommandBuffer, numGroupsX, numGroupsY, numGroupsZ);
}

void CommandContext::AddBarrier(const VkBufferMemoryBarrier2& barrier, bool bFlushImmediate)
{
	m_BufferBarriers[m_NumBufferBarriersToFlush++] = barrier;

	if (bFlushImmediate || m_NumBufferBarriersToFlush == MAX_NUM_PENDING_BARRIERS)
	{
		FlushBarriers();
	}
}

void CommandContext::AddBarrier(const VkImageMemoryBarrier2& barrier, bool bFlushImmediate)
{
	m_ImageBarriers[m_NumImageBarriersToFlush++] = barrier;

	if (bFlushImmediate || m_NumImageBarriersToFlush == MAX_NUM_PENDING_BARRIERS)
	{
		FlushBarriers();
	}
}

void CommandContext::FlushBarriers()
{
	if (m_NumBufferBarriersToFlush > 0)
	{
		VkDependencyInfo dependency = {};
		dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.bufferMemoryBarrierCount = m_NumBufferBarriersToFlush;
		dependency.pBufferMemoryBarriers = m_BufferBarriers;
		vkCmdPipelineBarrier2(m_vkCommandBuffer, &dependency);

		m_NumBufferBarriersToFlush = 0;
	}

	if (m_NumImageBarriersToFlush > 0)
	{
		VkDependencyInfo dependency = {};
		dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.imageMemoryBarrierCount = m_NumImageBarriersToFlush;
		dependency.pImageMemoryBarriers = m_ImageBarriers;
		vkCmdPipelineBarrier2(m_vkCommandBuffer, &dependency);

		m_NumImageBarriersToFlush = 0;
	}
}

} // namespace vk