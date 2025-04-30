#include "RendererPch.h"
#include "VkCommandBuffer.h"
#include "VkCommandQueue.h"
#include "VkRenderPipeline.h"
#include "VkUploadBufferPool.h"
#include "RenderResource/VkBuffer.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkTexture.h"

namespace vk
{

static PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR;

CommandBuffer::CommandBuffer(RenderContext& context, VkCommandPool vkCommandPool, VkCommandBufferLevel level)
	: m_renderContext(context)
    , m_vkBelongedPool(vkCommandPool)
    , m_level(level)
{
    VkDevice device = m_renderContext.vkDevice();
	vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)vkGetInstanceProcAddr(m_renderContext.vkInstance(), "vkCmdPushDescriptorSetKHR");

    // **
    // Allocate command buffer
    // **
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_vkBelongedPool;
    allocInfo.level = m_level;
    allocInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &m_vkCommandBuffer));


	// **
	// Create dynamic buffer pool
	// **
	m_pUploadBufferPool = new UploadBufferPool(m_renderContext);


    // **
    // Create sync-objects
    // **
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(m_renderContext.vkDevice(), &fenceInfo, nullptr, &m_vkFence);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(m_renderContext.vkDevice(), &semaphoreInfo, nullptr, &m_vkRenderCompleteSemaphore));
    VK_CHECK(vkCreateSemaphore(m_renderContext.vkDevice(), &semaphoreInfo, nullptr, &m_vkPresentCompleteSemaphore));
}

CommandBuffer::~CommandBuffer()
{
	RELEASE(m_pUploadBufferPool);

    vkDestroySemaphore(m_renderContext.vkDevice(), m_vkPresentCompleteSemaphore, nullptr);
    vkDestroySemaphore(m_renderContext.vkDevice(), m_vkRenderCompleteSemaphore, nullptr);
    vkDestroyFence(m_renderContext.vkDevice(), m_vkFence, nullptr);

    vkFreeCommandBuffers(m_renderContext.vkDevice(), m_vkBelongedPool, 1, &m_vkCommandBuffer);
}

void CommandBuffer::Open(VkCommandBufferUsageFlags flags)
{
    m_currentContextIndex = m_renderContext.ContextIndex();

    VK_CHECK(vkResetCommandBuffer(m_vkCommandBuffer, 0));
    VK_CHECK(vkResetFences(m_renderContext.vkDevice(), 1, &m_vkFence));

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;
    VK_CHECK(vkBeginCommandBuffer(m_vkCommandBuffer, &beginInfo));

	m_pUploadBufferPool->Reset();
	for (auto& allocation : m_allocations)
		allocation.clear();

	m_pGraphicsPipeline = nullptr;
	m_pComputePipeline = nullptr;
}

void CommandBuffer::Close()
{
    FlushBarriers();
    VK_CHECK(vkEndCommandBuffer(m_vkCommandBuffer));
}

bool CommandBuffer::IsFenceComplete() const
{
    return vkGetFenceStatus(m_renderContext.vkDevice(), m_vkFence) == VK_SUCCESS;
}

void CommandBuffer::WaitForFence() const
{
	vkWaitForFences(m_renderContext.vkDevice(), 1, &m_vkFence, VK_TRUE, UINT64_MAX);
}

void CommandBuffer::CopyBuffer(Buffer* pDstBuffer, Buffer* pSrcBuffer, VkDeviceSize dstOffset, VkDeviceSize srcOffset)
{
    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = pDstBuffer->SizeInBytes();
    vkCmdCopyBuffer(m_vkCommandBuffer, pDstBuffer->vkBuffer(), pSrcBuffer->vkBuffer(), 1, &copyRegion);

	VkBufferMemoryBarrier copyBarrier = {};
	copyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	copyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	copyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	copyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	copyBarrier.buffer = pDstBuffer->vkBuffer();
	copyBarrier.offset = 0;
	copyBarrier.size = pDstBuffer->SizeInBytes();
	vkCmdPipelineBarrier(m_vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &copyBarrier, 0, nullptr);
}

void CommandBuffer::CopyBuffer(Texture* pDstTexture, Buffer* pSrcBuffer, const std::vector< VkBufferImageCopy >& regions, bool bAllSubresources)
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
        VK_PIPELINE_STAGE_HOST_BIT, 
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        subresourceRange);

    vkCmdCopyBufferToImage(m_vkCommandBuffer, pSrcBuffer->vkBuffer(), pDstTexture->vkImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<u32>(regions.size()), regions.data());

    TransitionImageLayout(
        pDstTexture, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        subresourceRange);
}

void CommandBuffer::CopyTexture(Texture* pDstTexture, Texture* pSrcTexture)
{
	TransitionImageLayout(
		pSrcTexture, 
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
		VK_PIPELINE_STAGE_2_HOST_BIT, 
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, 
		pSrcTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, true);
	TransitionImageLayout(
		pDstTexture,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_2_HOST_BIT,
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

void CommandBuffer::GenerateMips(Texture* pTexture)
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
			VK_PIPELINE_STAGE_TRANSFER_BIT, 
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
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			subresourceRange);
	}

	subresourceRange.baseMipLevel = desc.mipLevels - 1;
	TransitionImageLayout(
		pTexture,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		subresourceRange);
}

void CommandBuffer::TransitionImageLayout(
	Texture* pTexture, 
	VkImageLayout newLayout, 
	VkPipelineStageFlags2 srcStageMask,
	VkPipelineStageFlags2 dstStageMask,
	VkImageAspectFlags aspectMask,
	bool bFlushImmediate, 
	bool bFlatten)
{
	TransitionImageLayout(
		pTexture, 
		newLayout, 
		srcStageMask, 
		dstStageMask, 
		{ aspectMask, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }, bFlushImmediate, bFlatten);
}

void CommandBuffer::TransitionImageLayout(
    Texture* pTexture, 
    VkImageLayout newLayout, 
	VkPipelineStageFlags2 srcStageMask,
	VkPipelineStageFlags2 dstStageMask,
    VkImageSubresourceRange subresourceRange, 
	bool bFlushImmediate, 
	bool bFlatten)
{
	VkImageMemoryBarrier2 imageMemoryBarrier = {};
	imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.srcAccessMask = 0;
	imageMemoryBarrier.dstAccessMask = 0;
	imageMemoryBarrier.srcStageMask = srcStageMask;
	imageMemoryBarrier.dstStageMask = dstStageMask;
	imageMemoryBarrier.oldLayout = pTexture->GetState().GetSubresourceState(subresourceRange);
	imageMemoryBarrier.newLayout = newLayout;
	imageMemoryBarrier.image = pTexture->vkImage();
	imageMemoryBarrier.subresourceRange = subresourceRange;

	switch (pTexture->GetState().GetSubresourceState(subresourceRange))
	{
	case VK_IMAGE_LAYOUT_UNDEFINED:
		// Only valid as initial layout
		// No flags required, listed only for completeness
		imageMemoryBarrier.srcAccessMask = 0;
		break;

	case VK_IMAGE_LAYOUT_GENERAL:
		// Assume this layout is used for write to image only
		// Make sure writing operation by the shader needs to be completed
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_PREINITIALIZED:
		// Only valid as initial layout for linear images, preserves memory contents
		// Make sure host writes have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		// Make sure any writes to the color buffer have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Make sure any writes to the depth/stencil buffer have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		// Make sure any reads from the image have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		// Make sure any writes to the image have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// Make sure any shader reads from the image have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		break;
	}

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

	if (pTexture)
	{
		if (bFlatten)
		{
			pTexture->FlattenSubresourceStates();
		}

		const auto& stateBefore = pTexture->GetState();
		if (stateBefore.GetSubresourceState(subresourceRange) != newLayout)
		{
			AddBarrier(imageMemoryBarrier, bFlushImmediate);

			pTexture->SetState(newLayout, subresourceRange);
		}
	}
}

void CommandBuffer::SetGraphicsPushConstants(u32 sizeInBytes, void* data, VkShaderStageFlags stages, u32 offsetInBytes)
{
	assert(m_pGraphicsPipeline);
	vkCmdPushConstants(m_vkCommandBuffer, m_pGraphicsPipeline->vkPipelineLayout(), stages, offsetInBytes, sizeInBytes, data);
}

void CommandBuffer::SetGraphicsDynamicUniformBuffer(u32 set, u32 binding, VkDeviceSize sizeInBytes, const void* bufferData)
{
	auto allocation = m_pUploadBufferPool->Allocate(sizeInBytes);
	memcpy(allocation.cpuHandle, bufferData, sizeInBytes);

	VkDescriptorBufferInfo bufferInfo = {};
	bufferInfo.buffer = allocation.vkBuffer;
	bufferInfo.offset = allocation.offset;
	bufferInfo.range = allocation.size;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = VK_NULL_HANDLE;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	write.pBufferInfo = &bufferInfo;
	
	assert(set > eDescriptorSet_Static);
	m_allocations[set].push_back({ binding, bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER });
}

void CommandBuffer::SetDescriptors(u32 set, u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType)
{
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = VK_NULL_HANDLE;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = descriptorType;
	write.pImageInfo = &imageInfo;

	assert(set > eDescriptorSet_Static);
	m_allocations[set].push_back({ binding, imageInfo, descriptorType });
}

void CommandBuffer::SetDescriptors(u32 set, u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType)
{
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = VK_NULL_HANDLE;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = descriptorType;
	write.pBufferInfo = &bufferInfo;

	assert(set > eDescriptorSet_Static);
	m_allocations[set].push_back({ binding, bufferInfo, descriptorType });
}

void CommandBuffer::SetRenderPipeline(GraphicsPipeline* pRenderPipeline)
{
	m_pComputePipeline = nullptr;
	if (pRenderPipeline && m_pGraphicsPipeline != pRenderPipeline)
	{
		m_pGraphicsPipeline = pRenderPipeline;
		vkCmdBindPipeline(m_vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pGraphicsPipeline->vkPipeline());
	}
}

void CommandBuffer::SetRenderPipeline(ComputePipeline* pRenderPipeline)
{
	m_pGraphicsPipeline = nullptr;
	if (pRenderPipeline && m_pComputePipeline != pRenderPipeline)
	{
		m_pComputePipeline = pRenderPipeline;
		vkCmdBindPipeline(m_vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pComputePipeline->vkPipeline());
	}
}

void CommandBuffer::BeginRenderPass(const RenderTarget& renderTarget)
{
	auto viewport = renderTarget.GetViewport();
	vkCmdSetViewport(m_vkCommandBuffer, 0, 1, &viewport);

	auto scissor = renderTarget.GetScissorRect();
	vkCmdSetScissor(m_vkCommandBuffer, 0, 1, &scissor);

	auto beginInfo = renderTarget.GetBeginInfo();
	vkCmdBeginRenderPass(m_vkCommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void CommandBuffer::EndRenderPass()
{
	vkCmdEndRenderPass(m_vkCommandBuffer);
}

void CommandBuffer::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
	std::vector< VkWriteDescriptorSet > writes;
	for (const auto& allocation : m_allocations[eDescriptorSet_Push])
	{
		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = VK_NULL_HANDLE;
		write.dstBinding = allocation.binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = allocation.descriptorType;
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

void CommandBuffer::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
	std::vector< VkWriteDescriptorSet > writes;
	for (const auto& allocation : m_allocations[eDescriptorSet_Push])
	{
		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = VK_NULL_HANDLE;
		write.dstBinding = allocation.binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = allocation.descriptorType;
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

void CommandBuffer::AddBarrier(const VkImageMemoryBarrier2& barrier, bool bFlushImmediate)
{
	m_imageBarriers[m_numBarriersToFlush++] = barrier;

	if (bFlushImmediate || m_numBarriersToFlush == MAX_NUM_PENDING_BARRIERS)
	{
		FlushBarriers();
	}
}

void CommandBuffer::FlushBarriers()
{
	if (m_numBarriersToFlush > 0)
	{
		VkDependencyInfo dependency = {};
		dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.imageMemoryBarrierCount = m_numBarriersToFlush;
		dependency.pImageMemoryBarriers = m_imageBarriers;
		vkCmdPipelineBarrier2(m_vkCommandBuffer, &dependency);

		m_numBarriersToFlush = 0;
	}
}

} // namespace vk