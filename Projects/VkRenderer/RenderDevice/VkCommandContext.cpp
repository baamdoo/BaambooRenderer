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

//-------------------------------------------------------------------------
// Impl
//-------------------------------------------------------------------------
class VkCommandContext::Impl
{
public:
	Impl(VkRenderDevice& rd, VkCommandPool vkCommandPool, eCommandType type, VkCommandBufferLevel vkLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	virtual ~Impl();

	void Open(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
	void Close();

	void CopyBuffer(
		VkBuffer vkDstBuffer,
		VkBuffer vkSrcBuffer,
		VkDeviceSize sizeInBytes,
		VkPipelineStageFlags2 dstStageMask,
		VkDeviceSize dstOffset = 0,
		VkDeviceSize srcOffset = 0,
		bool bFlushImmediate = true);
	void CopyBuffer(
		Arc< VulkanBuffer > pDstBuffer,
		Arc< VulkanBuffer > pSrcBuffer,
		VkDeviceSize sizeInBytes,
		VkPipelineStageFlags2 dstStageMask,
		VkDeviceSize dstOffset = 0,
		VkDeviceSize srcOffset = 0,
		bool bFlushImmediate = true);
	void CopyBuffer(
		Arc< VulkanTexture > pDstTexture,
		Arc< VulkanBuffer > pSrcBuffer,
		const std::vector< VkBufferImageCopy >& regions,
		bool bAllSubresources = true);
	void CopyTexture(Arc< VulkanTexture > pDstTexture, Arc< VulkanTexture > pSrcTexture);
	void BlitTexture(Arc< VulkanTexture > pDstTexture, Arc< VulkanTexture > pSrcTexture);
	void GenerateMips(Arc< VulkanTexture > pTexture);

	// todo. unlock other types of barrier
	void TransitionImageLayout(
		Arc< VulkanTexture > pTexture,
		VkImageLayout newLayout,
		VkImageAspectFlags aspectMask,
		bool bFlushImmediate = true,
		bool bFlatten = false);
	void TransitionImageLayout(
		Arc< VulkanTexture > pTexture,
		VkImageLayout newLayout,
		VkImageSubresourceRange subresourceRange,
		bool bFlushImmediate = true,
		bool bFlatten = false);

	void ClearTexture(
		Arc< VulkanTexture > pTexture,
		VkImageLayout newLayout,
		u32 baseMip = 0, u32 numMips = 1, u32 baseArray = 0, u32 numArrays = 1);

	void SetComputeDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData);
	void SetGraphicsDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData);

	void SetPushConstants(u32 sizeInBytes, const void* pData, VkShaderStageFlags stages, u32 offsetInBytes = 0);
	void SetDynamicUniformBuffer(u32 binding, VkDeviceSize sizeInBytes, const void* pData);
	
	void SetComputeShaderResource(const std::string& name, Arc< VulkanTexture > pTexture, Arc< VulkanSampler > samplerInCharge);
	void SetGraphicsShaderResource(const std::string& name, Arc< VulkanTexture > pTexture, Arc< VulkanSampler > samplerInCharge);
	void SetComputeShaderResource(const std::string& name, Arc< VulkanBuffer > pBuffer);
	void SetGraphicsShaderResource(const std::string& name, Arc< VulkanBuffer > pBuffer);
	 
	void StageDescriptor(const std::string& name, Arc< VulkanTexture > pTexture, Arc< VulkanSampler > pSamplerInCharge, u32 offset = 0);
	void StageDescriptor(const std::string& name, Arc< VulkanBuffer > pBuffer, u32 offset = 0);

	void PushDescriptor(u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType);
	void PushDescriptor(u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType);

	void SetRenderPipeline(VulkanGraphicsPipeline* pRenderPipeline);
	void SetRenderPipeline(VulkanComputePipeline* pRenderPipeline);

	void BeginRenderPass(const VulkanRenderTarget& renderTarget);
	void EndRenderPass();
	void BeginRendering(const VkRenderingInfo& renderInfo);
	void EndRendering();

	void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0);
	void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0);
	void DrawScene(const VkSceneResource& sceneResource);

	void Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ);

	template< u32 numThreadsPerGroupX >
	void Dispatch1D(u32 numThreadsX)
	{
		u32 numGroupsX = RoundUpAndDivide(numThreadsX, numThreadsPerGroupX);
		Dispatch(numGroupsX, 1, 1);
	}

	template< u32 numThreadsPerGroupX, u32 numThreadsPerGroupY >
	void Dispatch2D(u32 numThreadsX, u32 numThreadsY)
	{
		u32 numGroupsX = RoundUpAndDivide(numThreadsX, numThreadsPerGroupX);
		u32 numGroupsY = RoundUpAndDivide(numThreadsY, numThreadsPerGroupY);
		Dispatch(numGroupsX, numGroupsY, 1);
	}

	template< u32 numThreadsPerGroupX, u32 numThreadsPerGroupY, u32 numThreadsPerGroupZ >
	void Dispatch3D(u32 numThreadsX, u32 numThreadsY, u32 numThreadsZ)
	{
		u32 numGroupsX = RoundUpAndDivide(numThreadsX, numThreadsPerGroupX);
		u32 numGroupsY = RoundUpAndDivide(numThreadsY, numThreadsPerGroupY);
		u32 numGroupsZ = RoundUpAndDivide(numThreadsZ, numThreadsPerGroupZ);
		Dispatch(numGroupsX, numGroupsY, numGroupsZ);
	}

	[[nodiscard]]
	bool IsReady() const;
	[[nodiscard]]
	bool IsFenceComplete(VkFence vkFence) const;
	void WaitForFence(VkFence vkFence) const;
	void Flush() const;

	eCommandType GetCommandType() const { return m_CommandType; }

	bool IsTransient() const { return m_bTransient; }
	void SetTransient(bool bTransient) { m_bTransient = bTransient; }

	bool IsGraphicsContext() const { return m_pGraphicsPipeline != nullptr; }
	bool IsComputeContext() const { return m_pComputePipeline != nullptr; }

	VkCommandBuffer vkCommandBuffer() const { return m_vkCommandBuffer; }

	VkFence vkRenderCompleteFence() const { return m_vkRenderCompleteFence; }
	VkSemaphore vkRenderCompleteSemaphore() const { return m_vkRenderCompleteSemaphore; }
	VkFence vkPresentCompleteFence() const { return m_vkPresentCompleteFence; }
	VkSemaphore vkPresentCompleteSemaphore() const { return m_vkPresentCompleteSemaphore; }

	VkPipelineLayout vkGraphicsPipelineLayout() const { return m_pGraphicsPipeline ? m_pGraphicsPipeline->vkPipelineLayout() : nullptr; }
	VkPipelineLayout vkComputePipelineLayout() const { return m_pComputePipeline ? m_pComputePipeline->vkPipelineLayout() : nullptr; }
	VkPipeline vkGraphicsPipeline() const { return m_pGraphicsPipeline ? m_pGraphicsPipeline->vkPipeline() : nullptr; }
	VkPipeline vkComputePipeline() const { return m_pComputePipeline ? m_pComputePipeline->vkPipeline() : nullptr; }

private:
	void AddBarrier(const VkBufferMemoryBarrier2& barrier, bool bFlushImmediate);
	void AddBarrier(const VkImageMemoryBarrier2& barrier, bool bFlushImmediate);
	void FlushBarriers();

	template< typename T >
	constexpr T RoundUpAndDivide(T Value, size_t Alignment)
	{
		return (T)((Value + Alignment - 1) / Alignment);
	}

private:
	friend class CommandQueue;
	VkRenderDevice& m_RenderDevice;
	eCommandType    m_CommandType;

	VkCommandBuffer      m_vkCommandBuffer = VK_NULL_HANDLE;
	VkCommandPool        m_vkBelongedPool = VK_NULL_HANDLE;
	VkCommandBufferLevel m_Level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	DynamicBufferAllocator* m_pUniformBufferPool = nullptr;

	VkFence     m_vkRenderCompleteFence      = VK_NULL_HANDLE;
	VkSemaphore m_vkRenderCompleteSemaphore  = VK_NULL_HANDLE;
	VkFence     m_vkPresentCompleteFence     = VK_NULL_HANDLE;
	VkSemaphore m_vkPresentCompleteSemaphore = VK_NULL_HANDLE;

	VulkanGraphicsPipeline* m_pGraphicsPipeline = nullptr;
	VulkanComputePipeline*  m_pComputePipeline  = nullptr;

	struct AllocationInfo
	{
		u32              binding;
		DescriptorInfo   descriptor;
		VkDescriptorType descriptorType;
	};
	std::vector< AllocationInfo > m_PushAllocations;

	u32                    m_NumBufferBarriersToFlush = 0;
	VkBufferMemoryBarrier2 m_BufferBarriers[MAX_NUM_PENDING_BARRIERS] = {};
	u32                    m_NumImageBarriersToFlush = 0;
	VkImageMemoryBarrier2  m_ImageBarriers[MAX_NUM_PENDING_BARRIERS] = {};

	u32 m_CurrentContextIndex = 0;

	bool m_bTransient = false;
};

VkCommandContext::Impl::Impl(VkRenderDevice& rd, VkCommandPool vkCommandPool, eCommandType type, VkCommandBufferLevel level)
	: m_RenderDevice(rd)
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

VkCommandContext::Impl::~Impl()
{
	RELEASE(m_pUniformBufferPool);

	vkDestroySemaphore(m_RenderDevice.vkDevice(), m_vkPresentCompleteSemaphore, nullptr);
	vkDestroySemaphore(m_RenderDevice.vkDevice(), m_vkRenderCompleteSemaphore, nullptr);
	vkDestroyFence(m_RenderDevice.vkDevice(), m_vkPresentCompleteFence, nullptr);
	vkDestroyFence(m_RenderDevice.vkDevice(), m_vkRenderCompleteFence, nullptr);

	vkFreeCommandBuffers(m_RenderDevice.vkDevice(), m_vkBelongedPool, 1, &m_vkCommandBuffer);
}

void VkCommandContext::Impl::Open(VkCommandBufferUsageFlags flags)
{
	m_CurrentContextIndex = m_RenderDevice.ContextIndex();

	VkFence vkFences[2] = { m_vkRenderCompleteFence, m_vkPresentCompleteFence };
	VK_CHECK(vkResetFences(m_RenderDevice.vkDevice(), 2, vkFences));
	VK_CHECK(vkResetCommandBuffer(m_vkCommandBuffer, 0));

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = flags;
	VK_CHECK(vkBeginCommandBuffer(m_vkCommandBuffer, &beginInfo));

	m_pUniformBufferPool->Reset();
	m_PushAllocations.clear();

	m_pGraphicsPipeline = nullptr;
	m_pComputePipeline  = nullptr;
}

void VkCommandContext::Impl::Close()
{
	FlushBarriers();
	VK_CHECK(vkEndCommandBuffer(m_vkCommandBuffer));
}

bool VkCommandContext::Impl::IsReady() const
{
	return IsFenceComplete(m_vkRenderCompleteFence) && IsFenceComplete(m_vkPresentCompleteFence);
}

bool VkCommandContext::Impl::IsFenceComplete(VkFence vkFence) const
{
	return vkGetFenceStatus(m_RenderDevice.vkDevice(), vkFence) == VK_SUCCESS;
}

void VkCommandContext::Impl::WaitForFence(VkFence vkFence) const
{
	VK_CHECK(vkWaitForFences(m_RenderDevice.vkDevice(), 1, &vkFence, VK_TRUE, UINT64_MAX));
}

void VkCommandContext::Impl::Flush() const
{
	WaitForFence(m_vkRenderCompleteFence);
	WaitForFence(m_vkPresentCompleteFence);
}

void VkCommandContext::Impl::CopyBuffer(
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
		copyBarrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		copyBarrier.srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		copyBarrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
		copyBarrier.dstStageMask        = dstStageMask;
		copyBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
		copyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarrier.buffer              = vkDstBuffer;
		copyBarrier.offset              = 0;
		copyBarrier.size                = sizeInBytes;
		AddBarrier(copyBarrier, bFlushImmediate);
	}
}

void VkCommandContext::Impl::CopyBuffer(
	Arc< VulkanBuffer > pDstBuffer,
	Arc< VulkanBuffer > pSrcBuffer,
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

void VkCommandContext::Impl::CopyBuffer(Arc< VulkanTexture > pDstTexture, Arc< VulkanBuffer > pSrcBuffer, const std::vector< VkBufferImageCopy >& regions, bool bAllSubresources)
{
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel   = 0;
	subresourceRange.levelCount     = bAllSubresources ? VK_REMAINING_MIP_LEVELS : pDstTexture->Desc().mipLevels;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount     = bAllSubresources ? VK_REMAINING_ARRAY_LAYERS : pDstTexture->Desc().arrayLayers;

	TransitionImageLayout(
		pDstTexture,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		subresourceRange);

	vkCmdCopyBufferToImage(m_vkCommandBuffer, pSrcBuffer->vkBuffer(), pDstTexture->vkImage(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<u32>(regions.size()), regions.data());

	TransitionImageLayout(
		pDstTexture,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		subresourceRange);
}

void VkCommandContext::Impl::CopyTexture(Arc< VulkanTexture > pDstTexture, Arc< VulkanTexture > pSrcTexture)
{
	TransitionImageLayout(
		pSrcTexture,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		pSrcTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, true);
	TransitionImageLayout(
		pDstTexture,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		pDstTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, true);

	VkImageCopy copyRegion{};
	copyRegion.srcSubresource.aspectMask     = pSrcTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
	copyRegion.srcSubresource.mipLevel       = 0;
	copyRegion.srcSubresource.baseArrayLayer = 0;
	copyRegion.srcSubresource.layerCount     = 1;
	copyRegion.srcOffset                     = { 0, 0, 0 };

	copyRegion.dstSubresource.aspectMask     = pDstTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
	copyRegion.dstSubresource.mipLevel       = 0;
	copyRegion.dstSubresource.baseArrayLayer = 0;
	copyRegion.dstSubresource.layerCount     = 1;
	copyRegion.dstOffset                     = { 0, 0, 0 };

	copyRegion.extent = pSrcTexture->Desc().extent;

	vkCmdCopyImage(m_vkCommandBuffer, pSrcTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pDstTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
}

void VkCommandContext::Impl::BlitTexture(Arc< VulkanTexture > pDstTexture, Arc< VulkanTexture > pSrcTexture)
{
	VkImageAspectFlags format =
		pSrcTexture->Desc().format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
	TransitionImageLayout(
		pSrcTexture,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		format, true);
	TransitionImageLayout(
		pDstTexture,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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

void VkCommandContext::Impl::GenerateMips(Arc< VulkanTexture > pTexture)
{
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.levelCount     = 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount     = 1;

	const auto& desc = pTexture->Desc();
	for (u32 level = 0; level < desc.mipLevels - 1; ++level)
	{
		i32 w = desc.extent.width >> level;
		i32 h = desc.extent.height >> level;

		subresourceRange.baseMipLevel = level;
		TransitionImageLayout(
			pTexture,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			subresourceRange);

		VkImageBlit blit = {};
		blit.srcOffsets[0]                 = { 0, 0, 0 };
		blit.srcOffsets[1]                 = { w, h, 1 };
		blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel       = level;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount     = 1;
		blit.dstOffsets[0]                 = { 0, 0, 0 };
		blit.dstOffsets[1]                 = { w > 1 ? w / 2 : 1, h > 1 ? h / 2 : 1, 1 };
		blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel       = level + 1;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount     = 1;

		vkCmdBlitImage(m_vkCommandBuffer,
			pTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			pTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);

		TransitionImageLayout(
			pTexture,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			subresourceRange);
	}

	subresourceRange.baseMipLevel = desc.mipLevels - 1;
	TransitionImageLayout(
		pTexture,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		subresourceRange);
}

void VkCommandContext::Impl::TransitionImageLayout(
	Arc< VulkanTexture > pTexture,
	VkImageLayout newLayout,
	VkImageAspectFlags aspectMask,
	bool bFlushImmediate,
	bool bFlatten)
{
	TransitionImageLayout(
		pTexture,
		newLayout,
		{ aspectMask, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }, bFlushImmediate, bFlatten);
}

void VkCommandContext::Impl::TransitionImageLayout(
	Arc< VulkanTexture > pTexture,
	VkImageLayout newLayout,
	VkImageSubresourceRange subresourceRange,
	bool bFlushImmediate,
	bool bFlatten)
{
	assert(pTexture);

	VulkanTexture::State oldState = pTexture->GetState().GetSubresourceState(subresourceRange);
	if (oldState.layout == newLayout)
	{
		return;
	}

	VkImageMemoryBarrier2 imageMemoryBarrier = {};
	imageMemoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.srcAccessMask       = oldState.access;
	imageMemoryBarrier.dstAccessMask       = 0;
	imageMemoryBarrier.srcStageMask        = oldState.stage;
	imageMemoryBarrier.dstStageMask        = 0;
	imageMemoryBarrier.oldLayout           = oldState.layout;
	imageMemoryBarrier.newLayout           = newLayout;
	imageMemoryBarrier.image               = pTexture->vkImage();
	imageMemoryBarrier.subresourceRange    = subresourceRange;

	// Destination access mask controls the dependency for the new image layout
	switch (newLayout)
	{
	case VK_IMAGE_LAYOUT_GENERAL:
		// Assume this layout is used for write to image only
		imageMemoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		// Make sure any writes to the image have been finished
		imageMemoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		// Make sure any reads from the image have been finished
		imageMemoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		// Make sure any writes to the color buffer have been finished
		imageMemoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Make sure any writes to depth/stencil buffer have been finished
		imageMemoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
		imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// Make sure any writes to the image have been finished
		if (imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_HOST_BIT)
			imageMemoryBarrier.srcAccessMask |= VK_ACCESS_HOST_WRITE_BIT;
		if ((imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_COPY_BIT) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_BLIT_BIT) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_RESOLVE_BIT) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_CLEAR_BIT) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR))
			imageMemoryBarrier.srcAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstStageMask  = IsGraphicsContext() ?
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		imageMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		break;
	}
	
	VulkanTexture::State newState = 
	{
		.access = imageMemoryBarrier.dstAccessMask,
		.stage  = imageMemoryBarrier.dstStageMask,
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

void VkCommandContext::Impl::ClearTexture(
	Arc< VulkanTexture > pTexture,
	VkImageLayout newLayout,
	u32 baseMip, u32 numMips, u32 baseArray, u32 numArrays)
{
	VkImageSubresourceRange range = {};
	range.aspectMask     = pTexture->AspectMask();
	range.baseMipLevel   = baseMip;
	range.levelCount     = numMips;
	range.baseArrayLayer = baseArray;
	range.layerCount     = numArrays;

	TransitionImageLayout(pTexture, newLayout, range);
	if (pTexture->AspectMask() & VK_IMAGE_ASPECT_COLOR_BIT)
	{
		VkClearColorValue clearColor = pTexture->ClearValue().color;
		vkCmdClearColorImage(m_vkCommandBuffer, pTexture->vkImage(), pTexture->GetState().GetSubresourceState().layout, &clearColor, 1, &range);
	}
	else
	{
		VkClearDepthStencilValue clearDepthStencil = pTexture->ClearValue().depthStencil;
		vkCmdClearDepthStencilImage(m_vkCommandBuffer, pTexture->vkImage(), pTexture->GetState().GetSubresourceState().layout, &clearDepthStencil, 1, &range);
	}
}

void VkCommandContext::Impl::SetComputeDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData)
{
	assert(IsComputeContext());
	auto [_, binding] = m_pComputePipeline->GetResourceBindingIndex(name);

	SetDynamicUniformBuffer(binding, sizeInBytes, pData);
}

void VkCommandContext::Impl::SetGraphicsDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData)
{
	assert(IsGraphicsContext());
	auto [_, binding] = m_pGraphicsPipeline->GetResourceBindingIndex(name);

	SetDynamicUniformBuffer(binding, sizeInBytes, pData);
}

void VkCommandContext::Impl::SetPushConstants(u32 sizeInBytes, const void* pData, VkShaderStageFlags stages, u32 offsetInBytes)
{
	assert(m_pGraphicsPipeline || m_pComputePipeline);
	vkCmdPushConstants(m_vkCommandBuffer, m_pGraphicsPipeline ? m_pGraphicsPipeline->vkPipelineLayout() : m_pComputePipeline->vkPipelineLayout(), stages, offsetInBytes, sizeInBytes, pData);
}

void VkCommandContext::Impl::SetDynamicUniformBuffer(u32 binding, VkDeviceSize sizeInBytes, const void* pData)
{
	auto allocation = m_pUniformBufferPool->Allocate(sizeInBytes);
	memcpy(allocation.cpuHandle, pData, sizeInBytes);

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

void VkCommandContext::Impl::SetComputeShaderResource(const std::string& name, Arc< VulkanTexture > pTexture, Arc< VulkanSampler > pSamplerInCharge)
{
	assert(IsComputeContext());
	auto [_, binding] = m_pComputePipeline->GetResourceBindingIndex(name);

	auto layout = pTexture->GetState().GetSubresourceState().layout;

	VkDescriptorType descType =
		layout == VK_IMAGE_LAYOUT_GENERAL ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	PushDescriptor(
		binding,
		{
			.sampler     = pSamplerInCharge ? pSamplerInCharge->vkSampler() : VK_NULL_HANDLE,
			.imageView   = pTexture->vkView(),
			.imageLayout = layout
		}, descType);
}

void VkCommandContext::Impl::SetGraphicsShaderResource(const std::string& name, Arc< VulkanTexture > pTexture, Arc< VulkanSampler > pSamplerInCharge)
{
	assert(IsGraphicsContext());
	auto [_, binding] = m_pGraphicsPipeline->GetResourceBindingIndex(name);

	auto layout = pTexture->GetState().GetSubresourceState().layout;

	VkDescriptorType descType =
		layout == VK_IMAGE_LAYOUT_GENERAL ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	PushDescriptor(
		binding,
		{
			.sampler     = pSamplerInCharge ? pSamplerInCharge->vkSampler() : VK_NULL_HANDLE,
			.imageView   = pTexture->vkView(),
			.imageLayout = layout
		}, descType);
}

void VkCommandContext::Impl::SetComputeShaderResource(const std::string& name, Arc< VulkanBuffer > pBuffer)
{
	assert(IsComputeContext());
	auto [_, binding] = m_pComputePipeline->GetResourceBindingIndex(name);

	PushDescriptor(
		binding,
		{
			.buffer = pBuffer->vkBuffer(),
			.offset = 0,
			.range  = pBuffer->SizeInBytes()
		}, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

void VkCommandContext::Impl::SetGraphicsShaderResource(const std::string& name, Arc< VulkanBuffer > pBuffer)
{
	assert(IsGraphicsContext());
	auto [_, binding] = m_pGraphicsPipeline->GetResourceBindingIndex(name);

	PushDescriptor(
		binding,
		{
			.buffer = pBuffer->vkBuffer(),
			.offset = 0,
			.range  = pBuffer->SizeInBytes()
		}, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

void VkCommandContext::Impl::StageDescriptor(const std::string& name, Arc< VulkanTexture > pTexture, Arc< VulkanSampler > pSamplerInCharge, u32 offset)
{
	UNUSED(offset);

	if (IsGraphicsContext())
	{
		auto [_, binding] = m_pGraphicsPipeline->GetResourceBindingIndex(name);

		auto layout = pTexture->GetState().GetSubresourceState().layout;

		VkDescriptorType descType =
			layout == VK_IMAGE_LAYOUT_GENERAL ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		PushDescriptor(
			binding,
			{
				.sampler     = pSamplerInCharge ? pSamplerInCharge->vkSampler() : VK_NULL_HANDLE,
				.imageView   = pTexture->vkView(),
				.imageLayout = layout
			}, descType);
	}
	else if (IsComputeContext())
	{
		auto [_, binding] = m_pComputePipeline->GetResourceBindingIndex(name);

		auto layout = pTexture->GetState().GetSubresourceState().layout;

		VkDescriptorType descType =
			layout == VK_IMAGE_LAYOUT_GENERAL ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		PushDescriptor(
			binding,
			{
				.sampler     = pSamplerInCharge ? pSamplerInCharge->vkSampler() : VK_NULL_HANDLE,
				.imageView   = pTexture->vkView(),
				.imageLayout = layout
			}, descType);
	}
	else
	{
		assert(false && "No pipeline is set!");
	}
}

void VkCommandContext::Impl::StageDescriptor(const std::string& name, Arc< VulkanBuffer > pBuffer, u32 offset)
{
	if (IsGraphicsContext())
	{
		auto [_, binding] = m_pGraphicsPipeline->GetResourceBindingIndex(name);

		PushDescriptor(
			binding,
			{
				.buffer = pBuffer->vkBuffer(),
				.offset = offset,
				.range  = pBuffer->SizeInBytes()
			}, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	}
	else if (IsComputeContext())
	{
		auto [_, binding] = m_pComputePipeline->GetResourceBindingIndex(name);

		PushDescriptor(
			binding,
			{
				.buffer = pBuffer->vkBuffer(),
				.offset = offset,
				.range  = pBuffer->SizeInBytes()
			}, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	}
	else
	{
		assert(false && "No pipeline is set!");
	}
}

void VkCommandContext::Impl::PushDescriptor(u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType)
{
	VkWriteDescriptorSet write = {};
	write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet          = VK_NULL_HANDLE;
	write.dstBinding      = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType  = descriptorType;
	write.pImageInfo      = &imageInfo;

	m_PushAllocations.push_back({ binding, imageInfo, descriptorType });
}

void VkCommandContext::Impl::PushDescriptor(u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType)
{
	VkWriteDescriptorSet write = {};
	write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet          = VK_NULL_HANDLE;
	write.dstBinding      = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType  = descriptorType;
	write.pBufferInfo     = &bufferInfo;

	m_PushAllocations.push_back({ binding, bufferInfo, descriptorType });
}

void VkCommandContext::Impl::SetRenderPipeline(VulkanGraphicsPipeline* pRenderPipeline)
{
	m_pComputePipeline = nullptr;
	if (pRenderPipeline && m_pGraphicsPipeline != pRenderPipeline)
	{
		m_pGraphicsPipeline = pRenderPipeline;
		vkCmdBindPipeline(m_vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pGraphicsPipeline->vkPipeline());

		m_PushAllocations.clear();
	}
}

void VkCommandContext::Impl::SetRenderPipeline(VulkanComputePipeline* pRenderPipeline)
{
	m_pGraphicsPipeline = nullptr;
	if (pRenderPipeline && m_pComputePipeline != pRenderPipeline)
	{
		m_pComputePipeline = pRenderPipeline;
		vkCmdBindPipeline(m_vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pComputePipeline->vkPipeline());

		m_PushAllocations.clear();
	}
}

void VkCommandContext::Impl::BeginRenderPass(const VulkanRenderTarget& renderTarget)
{
	auto viewport = renderTarget.GetViewport();
	vkCmdSetViewport(m_vkCommandBuffer, 0, 1, &viewport);

	auto scissor = renderTarget.GetScissorRect();
	vkCmdSetScissor(m_vkCommandBuffer, 0, 1, &scissor);

	const auto& beginInfo = renderTarget.GetBeginInfo();
	vkCmdBeginRenderPass(m_vkCommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VkCommandContext::Impl::EndRenderPass()
{
	vkCmdEndRenderPass(m_vkCommandBuffer);
}

void VkCommandContext::Impl::BeginRendering(const VkRenderingInfo& renderInfo)
{
	vkCmdBeginRendering(m_vkCommandBuffer, &renderInfo);
}

void VkCommandContext::Impl::EndRendering()
{
	vkCmdEndRendering(m_vkCommandBuffer);
}

void VkCommandContext::Impl::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
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

void VkCommandContext::Impl::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
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

void VkCommandContext::Impl::DrawScene(const VkSceneResource& sceneResource)
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

	const auto& indirectInfo = sceneResource.GetIndirectBufferInfo();
	vkCmdBindIndexBuffer(m_vkCommandBuffer, sceneResource.GetIndexBufferInfo().buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexedIndirect(m_vkCommandBuffer, indirectInfo.buffer, indirectInfo.offset, u32(indirectInfo.range / sizeof(IndirectDrawData)), sizeof(IndirectDrawData));
}

void VkCommandContext::Impl::Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
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

void VkCommandContext::Impl::AddBarrier(const VkBufferMemoryBarrier2& barrier, bool bFlushImmediate)
{
	m_BufferBarriers[m_NumBufferBarriersToFlush++] = barrier;

	if (bFlushImmediate || m_NumBufferBarriersToFlush == MAX_NUM_PENDING_BARRIERS)
	{
		FlushBarriers();
	}
}

void VkCommandContext::Impl::AddBarrier(const VkImageMemoryBarrier2& barrier, bool bFlushImmediate)
{
	m_ImageBarriers[m_NumImageBarriersToFlush++] = barrier;

	if (bFlushImmediate || m_NumImageBarriersToFlush == MAX_NUM_PENDING_BARRIERS)
	{
		FlushBarriers();
	}
}

void VkCommandContext::Impl::FlushBarriers()
{
	if (m_NumBufferBarriersToFlush > 0)
	{
		VkDependencyInfo dependency = {};
		dependency.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.bufferMemoryBarrierCount = m_NumBufferBarriersToFlush;
		dependency.pBufferMemoryBarriers    = m_BufferBarriers;
		vkCmdPipelineBarrier2(m_vkCommandBuffer, &dependency);

		m_NumBufferBarriersToFlush = 0;
	}

	if (m_NumImageBarriersToFlush > 0)
	{
		VkDependencyInfo dependency = {};
		dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.imageMemoryBarrierCount = m_NumImageBarriersToFlush;
		dependency.pImageMemoryBarriers    = m_ImageBarriers;
		vkCmdPipelineBarrier2(m_vkCommandBuffer, &dependency);

		m_NumImageBarriersToFlush = 0;
	}
}


//-------------------------------------------------------------------------
// Command Context
//-------------------------------------------------------------------------
VkCommandContext::VkCommandContext(VkRenderDevice& rd, VkCommandPool vkCommandPool, eCommandType type, VkCommandBufferLevel level)
	: m_Impl(MakeBox< Impl >(rd, vkCommandPool, type, level)) {}

void VkCommandContext::Open(VkCommandBufferUsageFlags flags)
{
	m_Impl->Open(flags);
}

void VkCommandContext::Close()
{
	m_Impl->Close();
}

void VkCommandContext::CopyBuffer(
	VkBuffer vkDstBuffer,
	VkBuffer vkSrcBuffer,
	VkDeviceSize sizeInBytes,
	VkPipelineStageFlags2 dstStageMask,
	VkDeviceSize dstOffset,
	VkDeviceSize srcOffset,
	bool bFlushImmediate)
{
	m_Impl->CopyBuffer(vkDstBuffer, vkSrcBuffer, sizeInBytes, dstStageMask, dstOffset, srcOffset, bFlushImmediate);
}

void VkCommandContext::CopyBuffer(
	Arc< VulkanBuffer > dstBuffer,
	Arc< VulkanBuffer > srcBuffer,
	VkDeviceSize sizeInBytes,
	VkPipelineStageFlags2 dstStageMask,
	VkDeviceSize dstOffset,
	VkDeviceSize srcOffset,
	bool bFlushImmediate)
{
	m_Impl->CopyBuffer(dstBuffer, srcBuffer, sizeInBytes, dstStageMask, dstOffset, srcOffset, bFlushImmediate);
}

void VkCommandContext::CopyBuffer(
	Arc< VulkanTexture > dstTexture,
	Arc< VulkanBuffer > srcBuffer, 
	const std::vector< VkBufferImageCopy >& regions, 
	bool bAllSubresources)
{
	m_Impl->CopyBuffer(dstTexture, srcBuffer, regions, bAllSubresources);
}

void VkCommandContext::CopyBuffer(Arc< render::Buffer > dstBuffer, Arc< render::Buffer > srcBuffer)
{
	//m_Impl->CopyBuffer(dstBuffer, srcBuffer, srcBuffer->SizeInBytes(), )
}

void VkCommandContext::CopyTexture(Arc< render::Texture > dstTexture, Arc< render::Texture > srcTexture)
{
	auto vkTextureDst = StaticCast<VulkanTexture>(dstTexture);
	auto vkTextureSrc = StaticCast<VulkanTexture>(srcTexture);
	assert(vkTextureDst && vkTextureSrc);

	m_Impl->CopyTexture(vkTextureDst, vkTextureSrc);
}

void VkCommandContext::BlitTexture(Arc< VulkanTexture > dstTexture, Arc< VulkanTexture > srcTexture)
{
	m_Impl->BlitTexture(dstTexture, srcTexture);
}

void VkCommandContext::GenerateMips(Arc< VulkanTexture > texture)
{
	m_Impl->GenerateMips(texture);
}

void VkCommandContext::TransitionBarrier(Arc< render::Texture > texture, render::eTextureLayout newState, u32 subresource, bool bFlushImmediate)
{
	auto rhiTexture = StaticCast<VulkanTexture>(texture);
	assert(rhiTexture);

	TransitionImageLayout(rhiTexture, VK_LAYOUT(newState), texture->IsDepthTexture() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, bFlushImmediate);
}

void VkCommandContext::TransitionImageLayout(
	Arc< VulkanTexture > texture,
	VkImageLayout newLayout,
	VkImageAspectFlags aspectMask,
	bool bFlushImmediate,
	bool bFlatten)
{
	m_Impl->TransitionImageLayout(texture, newLayout, aspectMask, bFlushImmediate, bFlatten);
}

void VkCommandContext::TransitionImageLayout(
	Arc< VulkanTexture > texture,
	VkImageLayout newLayout,
	VkImageSubresourceRange subresourceRange,
	bool bFlushImmediate,
	bool bFlatten)
{
	m_Impl->TransitionImageLayout(texture, newLayout, subresourceRange, bFlushImmediate, bFlatten);
}

void VkCommandContext::ClearTexture(
	Arc< VulkanTexture > texture,
	VkImageLayout newLayout,
	u32 baseMip, u32 numMips, u32 baseArray, u32 numArrays)
{
	m_Impl->ClearTexture(texture, newLayout, baseMip, numMips, baseArray, numArrays);
}

void VkCommandContext::SetComputeConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes)
{
	m_Impl->SetPushConstants(sizeInBytes, pData, VK_SHADER_STAGE_COMPUTE_BIT, offsetInBytes);
}

void VkCommandContext::SetGraphicsConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes)
{
	m_Impl->SetPushConstants(sizeInBytes, pData, VK_SHADER_STAGE_ALL_GRAPHICS, offsetInBytes);
}

void VkCommandContext::SetComputeDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData)
{
	m_Impl->SetComputeDynamicUniformBuffer(name, sizeInBytes, pData);
}

void VkCommandContext::SetGraphicsDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData)
{
	m_Impl->SetGraphicsDynamicUniformBuffer(name, sizeInBytes, pData);
}

void VkCommandContext::SetComputeShaderResource(const std::string& name, Arc< render::Texture > texture, Arc< render::Sampler > samplerInCharge)
{
	auto rhiTexture = StaticCast<VulkanTexture>(texture);
	assert(rhiTexture);

	m_Impl->SetComputeShaderResource(name, rhiTexture, StaticCast<VulkanSampler>(samplerInCharge));
}

void VkCommandContext::SetGraphicsShaderResource(const std::string& name, Arc< render::Texture > texture, Arc< render::Sampler > samplerInCharge)
{
	auto rhiTexture = StaticCast<VulkanTexture>(texture);
	assert(rhiTexture);

	m_Impl->SetGraphicsShaderResource(name, rhiTexture, StaticCast<VulkanSampler>(samplerInCharge));
}

void VkCommandContext::SetComputeShaderResource(const std::string& name, Arc< render::Buffer > buffer)
{
	auto rhiBuffer = StaticCast<VulkanBuffer>(buffer);
	assert(rhiBuffer);

	m_Impl->SetComputeShaderResource(name, rhiBuffer);
}

void VkCommandContext::SetGraphicsShaderResource(const std::string& name, Arc< render::Buffer > buffer)
{
	auto rhiBuffer = StaticCast<VulkanBuffer>(buffer);
	assert(rhiBuffer);

	m_Impl->SetGraphicsShaderResource(name, rhiBuffer);
}

void VkCommandContext::StageDescriptor(const std::string& name, Arc< render::Buffer > buffer, u32 offset)
{
	auto rhiBuffer = StaticCast<VulkanBuffer>(buffer);
	assert(rhiBuffer);

	m_Impl->StageDescriptor(name, rhiBuffer, offset);
}

void VkCommandContext::StageDescriptor(const std::string& name, Arc< render::Texture > texture, Arc< render::Sampler > samplerInCharge, u32 offset)
{
	auto rhiTexture = StaticCast<VulkanTexture>(texture);
	assert(rhiTexture);

	m_Impl->StageDescriptor(name, rhiTexture, StaticCast<VulkanSampler>(samplerInCharge), offset);
}

void VkCommandContext::PushDescriptor(u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType)
{
	m_Impl->PushDescriptor(binding, imageInfo, descriptorType);
}

void VkCommandContext::PushDescriptor(u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType)
{
	m_Impl->PushDescriptor(binding, bufferInfo, descriptorType);
}

void VkCommandContext::SetRenderPipeline(render::GraphicsPipeline* pRenderPipeline)
{
	auto vkRenderPipeline = static_cast<VulkanGraphicsPipeline*>(pRenderPipeline);
	assert(vkRenderPipeline);

	m_Impl->SetRenderPipeline(vkRenderPipeline);
}

void VkCommandContext::SetRenderPipeline(render::ComputePipeline* pRenderPipeline)
{
	auto vkRenderPipeline = static_cast<VulkanComputePipeline*>(pRenderPipeline);
	assert(vkRenderPipeline);

	m_Impl->SetRenderPipeline(vkRenderPipeline);
}

void VkCommandContext::BeginRenderPass(Arc< render::RenderTarget > renderTarget)
{
	auto vkRenderTarget = StaticCast<VulkanRenderTarget>(renderTarget);
	assert(vkRenderTarget);

	m_Impl->BeginRenderPass(*vkRenderTarget);
}

void VkCommandContext::EndRenderPass()
{
	m_Impl->EndRenderPass();
}

void VkCommandContext::BeginRendering(const VkRenderingInfo& renderInfo)
{
	m_Impl->BeginRendering(renderInfo);
}

void VkCommandContext::EndRendering()
{
	m_Impl->EndRendering();
}

void VkCommandContext::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
	m_Impl->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void VkCommandContext::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
	m_Impl->DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VkCommandContext::DrawScene(const render::SceneResource& sceneResource)
{
	const auto& vkSceneResource = static_cast<const VkSceneResource&>(sceneResource);

	m_Impl->DrawScene(vkSceneResource);
}

void VkCommandContext::Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	m_Impl->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

bool VkCommandContext::IsReady() const
{
	return m_Impl->IsReady();
}

bool VkCommandContext::IsFenceComplete(VkFence vkFence) const
{
	return m_Impl->IsFenceComplete(vkFence);
}

void VkCommandContext::WaitForFence(VkFence vkFence) const
{
	m_Impl->WaitForFence(vkFence);
}

void VkCommandContext::Flush() const
{
	m_Impl->Flush();
}

eCommandType VkCommandContext::GetCommandType() const
{
	return m_Impl->GetCommandType();
}

bool VkCommandContext::IsTransient() const
{
	return m_Impl->IsTransient();
}

void VkCommandContext::SetTransient(bool bTransient)
{
	return m_Impl->SetTransient(bTransient);
}

bool VkCommandContext::IsGraphicsContext() const
{
	return m_Impl->IsGraphicsContext();
}

bool VkCommandContext::IsComputeContext() const
{
	return m_Impl->IsComputeContext();
}

VkCommandBuffer VkCommandContext::vkCommandBuffer() const
{
	return m_Impl->vkCommandBuffer();
}

VkFence VkCommandContext::vkRenderCompleteFence() const
{
	return m_Impl->vkRenderCompleteFence();
}

VkSemaphore VkCommandContext::vkRenderCompleteSemaphore() const
{
	return m_Impl->vkRenderCompleteSemaphore();
}

VkFence VkCommandContext::vkPresentCompleteFence() const
{
	return m_Impl->vkPresentCompleteFence();
}

VkSemaphore VkCommandContext::vkPresentCompleteSemaphore() const
{
	return m_Impl->vkPresentCompleteSemaphore();
}

VkPipelineLayout VkCommandContext::vkGraphicsPipelineLayout() const
{
	return m_Impl->vkGraphicsPipelineLayout();
}

VkPipelineLayout VkCommandContext::vkComputePipelineLayout() const
{
	return m_Impl->vkComputePipelineLayout();
}

VkPipeline VkCommandContext::vkGraphicsPipeline() const
{
	return m_Impl->vkGraphicsPipeline();
}

VkPipeline VkCommandContext::vkComputePipeline() const
{
	return m_Impl->vkComputePipeline();
}

} // namespace vk