#include "RendererPch.h"
#include "VkCommandContext.h"
#include "VkCommandQueue.h"
#include "VkRenderPipeline.h"
#include "VkBufferAllocator.h"
#include "VkResourceManager.h"
#include "VkDescriptorSet.h"
#include "VkTimer.h"

#include "RenderResource/VkBuffer.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSceneResource.h"

#include "Utils/Math.hpp"

namespace vk
{

static PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR;
static PFN_vkCmdDrawMeshTasksIndirectEXT vkCmdDrawMeshTasksIndirectEXT;
static PFN_vkCmdDrawMeshTasksIndirectCountEXT vkCmdDrawMeshTasksIndirectCountEXT;

//-------------------------------------------------------------------------
// Impl
//-------------------------------------------------------------------------
class VkCommandContext::Impl
{
public:
	Impl(VkRenderDevice& rd, VkCommandContext& context, VkCommandPool vkCommandPool, eCommandType type, VkCommandBufferLevel vkLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	virtual ~Impl();

	void Open(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
	void Close();

	void UploadData(const Arc< VulkanBuffer >& pDstBuffer, const void* pData, u32 numElements, u64 elemSizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes);
	void CopyBuffer(
		VkBuffer vkDstBuffer,
		VkBuffer vkSrcBuffer,
		VkDeviceSize sizeInBytes,
		VkPipelineStageFlags2 dstStageMask,
		VkDeviceSize dstOffset = 0,
		VkDeviceSize srcOffset = 0,
		bool bFlushImmediate = true);
	void CopyBuffer(
		const Arc< VulkanBuffer >& pDstBuffer,
		const Arc< VulkanBuffer >& pSrcBuffer,
		VkDeviceSize sizeInBytes,
		VkPipelineStageFlags2 dstStageMask,
		VkDeviceSize dstOffset = 0,
		VkDeviceSize srcOffset = 0,
		bool bFlushImmediate = true);
	void CopyBuffer(
		const Arc< VulkanTexture >& pDstTexture,
		const Arc< VulkanBuffer >& pSrcBuffer,
		const std::vector< VkBufferImageCopy >& regions,
		bool bAllSubresources = true);
	void CopyTexture(const Arc< VulkanTexture >& pDstTexture, const Arc< VulkanTexture >& pSrcTexture);
	void BlitTexture(const Arc< VulkanTexture >& pDstTexture, const Arc< VulkanTexture >& pSrcTexture);
	void GenerateMips(const Arc< VulkanTexture >& pTexture);

	void TransitionBarrier(
		const Arc< VulkanBuffer >& pBuffer,
		const BarrierState& newState,
		u64 offsetInBytes,  
		bool bFlushImmediate = false);
	void TransitionImageLayout(
		const Arc< VulkanTexture >& pTexture,
		VkImageLayout newLayout,
		VkImageAspectFlags aspectMask,
		bool bFlushImmediate = true,
		bool bFlatten = false);
	void TransitionImageLayout(
		const Arc< VulkanTexture >& pTexture,
		VkImageLayout newLayout,
		VkImageSubresourceRange subresourceRange,
		bool bFlushImmediate = true,
		bool bFlatten = false);
	void UAVBarrier(const Arc< VulkanBuffer >& pBuffer, bool bFlushImmediate);

	void FillBuffer(const Arc< VulkanBuffer >& pBuffer, u32 value, u64 offsetInBytes);
	void ClearTexture(
		Arc< VulkanTexture > pTexture,
		VkImageLayout newLayout,
		u32 baseMip = 0, u32 numMips = 1, u32 baseArray = 0, u32 numArrays = 1);

	void SetComputeDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData);
	void SetGraphicsDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData);

	void SetPushConstants(u32 sizeInBytes, const void* pData, VkShaderStageFlags stages, u32 offsetInBytes = 0);
	void SetDynamicUniformBuffer(u32 set, u32 binding, VkDeviceSize sizeInBytes, const void* pData);
	
	void SetComputeShaderResource(const std::string& name, Arc< VulkanTexture > pTexture, Arc< VulkanSampler > samplerInCharge);
	void SetGraphicsShaderResource(const std::string& name, Arc< VulkanTexture > pTexture, Arc< VulkanSampler > samplerInCharge);
	void SetComputeShaderResource(const std::string& name, Arc< VulkanBuffer > pBuffer);
	void SetGraphicsShaderResource(const std::string& name, Arc< VulkanBuffer > pBuffer);
	 
	void StageDescriptor(const std::string& name, Arc< VulkanTexture > pTexture, Arc< VulkanSampler > pSamplerInCharge, u32 offset = 0);
	void StageDescriptor(const std::string& name, Arc< VulkanBuffer > pBuffer, u32 offset = 0);

	void PushDescriptor(u32 set, u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType);
	void PushDescriptor(u32 set, u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType);

	void SetRenderPipeline(VulkanGraphicsPipeline* pRenderPipeline);
	void SetRenderPipeline(VulkanComputePipeline* pRenderPipeline);

	void BeginRenderPass(const VulkanRenderTarget& renderTarget);
	void EndRenderPass();
	void BeginRendering(const VkRenderingInfo& renderInfo);
	void EndRendering();

	void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0);
	void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0);
	void DrawMeshTasksIndirect(const Arc< VulkanBuffer >& pArgumentBuffer, u64 offsetInBytes, u32 numDraws, u32 strideInBytes);
	void DrawMeshTasksIndirectCount(const Arc< VulkanBuffer >& pArgumentBuffer, u64 offsetInBytes, const Arc< VulkanBuffer >& pCountBuffer, u32 numDraws, u32 strideInBytes);
	void Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ);

	[[nodiscard]]
	bool IsReady() const;
	[[nodiscard]]
	bool IsFenceComplete(VkFence vkFence) const;
	void WaitForFence(VkFence vkFence) const;
	void Flush() const;
	void FlushBarriers();

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

	double GetElapsedTime() const;

private:
	void AddBarrier(const VkBufferMemoryBarrier2& barrier, bool bFlushImmediate);
	void AddBarrier(const VkImageMemoryBarrier2& barrier, bool bFlushImmediate);

	void BindShaderResources(VkPipelineBindPoint bindPoint, VkPipelineLayout vkPipelineLayout);

	template< typename T >
	constexpr T RoundUpAndDivide(T Value, size_t Alignment)
	{
		return (T)((Value + Alignment - 1) / Alignment);
	}

private:
	friend class CommandQueue;
	VkRenderDevice& m_RenderDevice;
	eCommandType    m_CommandType;

	VkCommandContext& m_CommandContext;

	VkTimer m_Timer;

	VkCommandBuffer      m_vkCommandBuffer = VK_NULL_HANDLE;
	VkCommandPool        m_vkBelongedPool = VK_NULL_HANDLE;
	VkCommandBufferLevel m_Level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	Box< DynamicBufferAllocator > m_pUniformBufferPool;
	Box< DynamicBufferAllocator > m_pStagingBufferPool;

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
	std::unordered_map< u32, std::vector< AllocationInfo > > m_PushAllocations;

	u32                    m_NumBufferBarriersToFlush = 0;
	VkBufferMemoryBarrier2 m_BufferBarriers[MAX_NUM_PENDING_BARRIERS] = {};
	u32                    m_NumImageBarriersToFlush = 0;
	VkImageMemoryBarrier2  m_ImageBarriers[MAX_NUM_PENDING_BARRIERS] = {};

	u32 m_CurrentContextIndex = 0;

	bool m_bTransient = false;

	double m_LastFrameElapsedTime = 0.0;
};

VkCommandContext::Impl::Impl(VkRenderDevice& rd, VkCommandContext& context, VkCommandPool vkCommandPool, eCommandType type, VkCommandBufferLevel level)
	: m_RenderDevice(rd)
	, m_CommandType(type)
	, m_CommandContext(context)
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
	// Create buffer pools
	// **
	//m_pUniformBufferPool = MakeBox< DynamicBufferAllocator >(m_RenderDevice);
	m_pStagingBufferPool = MakeBox< DynamicBufferAllocator >(m_RenderDevice);


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


	// **
	// Set Gpu Timer
	// **
	m_Timer.Init(m_RenderDevice.vkDevice(), 2);
}

VkCommandContext::Impl::~Impl()
{
	m_Timer.Destroy(m_RenderDevice.vkDevice());

	vkDestroySemaphore(m_RenderDevice.vkDevice(), m_vkPresentCompleteSemaphore, nullptr);
	vkDestroySemaphore(m_RenderDevice.vkDevice(), m_vkRenderCompleteSemaphore, nullptr);
	vkDestroyFence(m_RenderDevice.vkDevice(), m_vkPresentCompleteFence, nullptr);
	vkDestroyFence(m_RenderDevice.vkDevice(), m_vkRenderCompleteFence, nullptr);

	vkFreeCommandBuffers(m_RenderDevice.vkDevice(), m_vkBelongedPool, 1, &m_vkCommandBuffer);
}

void VkCommandContext::Impl::Open(VkCommandBufferUsageFlags flags)
{
	Flush();

	m_LastFrameElapsedTime = m_Timer.GetElapsedTime(m_RenderDevice.vkDevice(), m_RenderDevice.DeviceProps());

	m_CurrentContextIndex = m_RenderDevice.ContextIndex();

	VkFence vkFences[2] = { m_vkRenderCompleteFence, m_vkPresentCompleteFence };
	VK_CHECK(vkResetFences(m_RenderDevice.vkDevice(), 2, vkFences));
	VK_CHECK(vkResetCommandBuffer(m_vkCommandBuffer, 0));

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = flags;
	VK_CHECK(vkBeginCommandBuffer(m_vkCommandBuffer, &beginInfo));

	//m_pUniformBufferPool->Reset();
	m_pStagingBufferPool->Reset();
	m_PushAllocations.clear();

	m_pGraphicsPipeline = nullptr;
	m_pComputePipeline  = nullptr;

	m_Timer.Start(m_vkCommandBuffer);
}

void VkCommandContext::Impl::Close()
{
	m_Timer.End(m_vkCommandBuffer);

	FlushBarriers();
	VK_CHECK(vkEndCommandBuffer(m_vkCommandBuffer));
}

void VkCommandContext::Impl::UploadData(const Arc< VulkanBuffer >& pDstBuffer, const void* pData, u32 numElements, u64 elemSizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes)
{
	u64 sizeInBytes = numElements * elemSizeInBytes;

	auto allocation = m_pStagingBufferPool->Allocate(sizeInBytes);
	memcpy(allocation.cpuHandle, pData, sizeInBytes);

	CopyBuffer(pDstBuffer, allocation.pBuffer, sizeInBytes, dstStageMask, dstOffsetInBytes, allocation.offsetInBytes, false);
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
	copyRegion.size      = sizeInBytes;
	vkCmdCopyBuffer(m_vkCommandBuffer, vkSrcBuffer, vkDstBuffer, 1, &copyRegion);

	if (!m_bTransient)
	{
		VkBufferMemoryBarrier2 copyBarrier = {};
		copyBarrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		copyBarrier.srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		copyBarrier.srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		copyBarrier.dstStageMask        = dstStageMask;
		copyBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		copyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarrier.buffer              = vkDstBuffer;
		copyBarrier.offset              = dstOffset;
		copyBarrier.size                = sizeInBytes;
		AddBarrier(copyBarrier, bFlushImmediate);
	}
}

void VkCommandContext::Impl::CopyBuffer(
	const Arc< VulkanBuffer >& pDstBuffer,
	const Arc< VulkanBuffer >& pSrcBuffer,
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

	pDstBuffer->SetState({ VK_ACCESS_2_SHADER_READ_BIT, dstStageMask });
}

void VkCommandContext::Impl::CopyBuffer(const Arc< VulkanTexture >& pDstTexture, const Arc< VulkanBuffer >& pSrcBuffer, const std::vector< VkBufferImageCopy >& regions, bool bAllSubresources)
{
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel   = 0;
	subresourceRange.levelCount     = bAllSubresources ? VK_REMAINING_MIP_LEVELS : 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount     = bAllSubresources ? VK_REMAINING_ARRAY_LAYERS : 1;

	TransitionImageLayout(
		pDstTexture,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		subresourceRange);

	vkCmdCopyBufferToImage(m_vkCommandBuffer, pSrcBuffer->vkBuffer(), pDstTexture->vkImage(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<u32>(regions.size()), regions.data());
}

void VkCommandContext::Impl::CopyTexture(const Arc< VulkanTexture >& pDstTexture, const Arc< VulkanTexture >& pSrcTexture)
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

void VkCommandContext::Impl::BlitTexture(const Arc< VulkanTexture >& pDstTexture, const Arc< VulkanTexture >& pSrcTexture)
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

void VkCommandContext::Impl::GenerateMips(const Arc< VulkanTexture >& pTexture)
{
	// Assume this function is executed right after copy (staging to texture) operation
	const auto& desc = pTexture->Desc();
	VkImageAspectFlags aspectMask = pTexture->IsDepthTexture() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask     = aspectMask;
	subresourceRange.levelCount     = 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount     = desc.arrayLayers;

	for (u32 level = 0; level < desc.mipLevels - 1; ++level)
	{
		i32 srcWidth  = desc.extent.width >> level;
		i32 srcHeight = desc.extent.height >> level;
		i32 srcDepth  = desc.extent.depth >> level;

		i32 dstWidth  = srcWidth > 1 ? srcWidth / 2 : 1;
		i32 dstHeight = srcHeight > 1 ? srcHeight / 2 : 1;
		i32 dstDepth  = srcDepth > 1 ? srcDepth / 2 : 1;

		subresourceRange.baseMipLevel = level;
		TransitionImageLayout(
			pTexture,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			subresourceRange);

		VkImageBlit blit = {};
		blit.srcOffsets[0]                 = { 0, 0, 0 };
		blit.srcOffsets[1]                 = { srcWidth, srcHeight, srcDepth };
		blit.srcSubresource.aspectMask     = aspectMask;
		blit.srcSubresource.mipLevel       = level;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount     = desc.arrayLayers;
		blit.dstOffsets[0]                 = { 0, 0, 0 };
		blit.dstOffsets[1]                 = { dstWidth, dstHeight, dstDepth };
		blit.dstSubresource.aspectMask     = aspectMask;
		blit.dstSubresource.mipLevel       = level + 1;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount     = desc.arrayLayers;

		vkCmdBlitImage(m_vkCommandBuffer,
			pTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			pTexture->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);
	}

	subresourceRange.baseMipLevel = desc.mipLevels - 1;
	TransitionImageLayout(
		pTexture,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		subresourceRange);
}

void VkCommandContext::Impl::TransitionBarrier(
	const Arc< VulkanBuffer >& pBuffer, 
	const BarrierState& newState,
	u64 offsetInBytes, 
	bool bFlushImmediate)
{
	if (!pBuffer)
		return;

	const auto& oldState = pBuffer->GetState().GetSubresourceState();
	if (oldState == newState)
		return;

	VkBufferMemoryBarrier2 barrier = {};
	barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	barrier.srcStageMask  = oldState.stage;
	barrier.dstStageMask  = newState.stage;
	barrier.srcAccessMask = oldState.access;
	barrier.dstAccessMask = newState.access;

	barrier.buffer = pBuffer->vkBuffer();
	barrier.offset = offsetInBytes;
	barrier.size   = VK_WHOLE_SIZE;

	pBuffer->SetState(newState);
	AddBarrier(barrier, bFlushImmediate);
}

void VkCommandContext::Impl::TransitionImageLayout(
	const Arc< VulkanTexture >& pTexture,
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
	const Arc< VulkanTexture >& pTexture,
	VkImageLayout newLayout,
	VkImageSubresourceRange subresourceRange,
	bool bFlushImmediate,
	bool bFlatten)
{
	assert(pTexture);

	BarrierState oldState = pTexture->GetState().GetSubresourceState(subresourceRange);
	if (oldState.layout == newLayout)
	{
		return;
	}

	VkImageMemoryBarrier2 imageMemoryBarrier = {};
	imageMemoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.srcAccessMask       = oldState.access;
	imageMemoryBarrier.srcStageMask        = oldState.stage;
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
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		// Make sure any writes to the image have been finished
		imageMemoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		// Make sure any reads from the image have been finished
		imageMemoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		break;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		// Make sure any writes to the color buffer have been finished
		imageMemoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Make sure any writes to depth/stencil buffer have been finished
		imageMemoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
		imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// Make sure any writes to the image have been finished
		if (imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_HOST_BIT)
			imageMemoryBarrier.srcAccessMask |= VK_ACCESS_2_HOST_WRITE_BIT;
		if ((imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_COPY_BIT) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_BLIT_BIT) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_RESOLVE_BIT) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_CLEAR_BIT) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR) ||
			(imageMemoryBarrier.srcStageMask & VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR))
			imageMemoryBarrier.srcAccessMask |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstStageMask  = IsGraphicsContext() ?
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
		break;
	default:
		imageMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		break;
	}

	BarrierState newState(imageMemoryBarrier.dstAccessMask, imageMemoryBarrier.dstStageMask, newLayout);
	pTexture->SetState(newState, subresourceRange);

	AddBarrier(imageMemoryBarrier, bFlushImmediate);
}

void VkCommandContext::Impl::UAVBarrier(const Arc< VulkanBuffer >& pBuffer, bool bFlushImmediate)
{
	if (!pBuffer)
		return;

	VkBufferMemoryBarrier2 barrier = {};
	barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	barrier.srcStageMask        = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.srcAccessMask       = VK_ACCESS_2_SHADER_WRITE_BIT;
	barrier.dstStageMask        = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer              = pBuffer->vkBuffer();
	barrier.offset              = 0;
	barrier.size                = VK_WHOLE_SIZE;
	pBuffer->SetState({ barrier.dstAccessMask, barrier.dstStageMask });

	AddBarrier(barrier, bFlushImmediate);
}

void VkCommandContext::Impl::FillBuffer(const Arc< VulkanBuffer >& pBuffer, u32 value, u64 offsetInBytes)
{
	vkCmdFillBuffer(m_vkCommandBuffer, pBuffer->vkBuffer(), offsetInBytes, pBuffer->SizeInBytes(), value);
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
	auto [set, binding] = m_pComputePipeline->GetResourceBindingIndex(name);
	if (IsValidIndex(set))
	{
		SetDynamicUniformBuffer(set, binding, sizeInBytes, pData);
	}
}

void VkCommandContext::Impl::SetGraphicsDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData)
{
	assert(IsGraphicsContext());
	auto [set, binding] = m_pGraphicsPipeline->GetResourceBindingIndex(name);

	SetDynamicUniformBuffer(set, binding, sizeInBytes, pData);
}

void VkCommandContext::Impl::SetPushConstants(u32 sizeInBytes, const void* pData, VkShaderStageFlags stages, u32 offsetInBytes)
{
	assert(m_pGraphicsPipeline || m_pComputePipeline);
	vkCmdPushConstants(m_vkCommandBuffer, m_pGraphicsPipeline ? m_pGraphicsPipeline->vkPipelineLayout() : m_pComputePipeline->vkPipelineLayout(), stages, offsetInBytes, sizeInBytes, pData);
}

void VkCommandContext::Impl::SetDynamicUniformBuffer(u32 set, u32 binding, VkDeviceSize sizeInBytes, const void* pData)
{
	auto allocation = m_pUniformBufferPool->Allocate(sizeInBytes);
	memcpy(allocation.cpuHandle, pData, sizeInBytes);

	VkDescriptorBufferInfo bufferInfo = {};
	bufferInfo.buffer = allocation.pBuffer->vkBuffer();
	bufferInfo.offset = allocation.offsetInBytes;
	bufferInfo.range  = allocation.pBuffer->SizeInBytes();

	m_PushAllocations[set].push_back({ binding, bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER });
}

void VkCommandContext::Impl::SetComputeShaderResource(const std::string& name, Arc< VulkanTexture > pTexture, Arc< VulkanSampler > pSamplerInCharge)
{
	assert(IsComputeContext());
	auto [set, binding] = m_pComputePipeline->GetResourceBindingIndex(name);

	auto layout = pTexture->GetState().GetSubresourceState().layout;

	VkDescriptorType descType =
		layout == VK_IMAGE_LAYOUT_GENERAL ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	PushDescriptor(
		set,
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
	auto [set, binding] = m_pGraphicsPipeline->GetResourceBindingIndex(name);

	auto layout = pTexture->GetState().GetSubresourceState().layout;

	VkDescriptorType descType =
		layout == VK_IMAGE_LAYOUT_GENERAL ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	PushDescriptor(
		set,
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
	auto [set, binding] = m_pComputePipeline->GetResourceBindingIndex(name);

	PushDescriptor(
		set,
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
	auto [set, binding] = m_pGraphicsPipeline->GetResourceBindingIndex(name);

	PushDescriptor(
		set,
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
		auto [set, binding] = m_pGraphicsPipeline->GetResourceBindingIndex(name);
		if (set == INVALID_INDEX || binding == INVALID_INDEX)
		{
			__debugbreak();
		}

		auto layout = pTexture->GetState().GetSubresourceState().layout;

		VkDescriptorType descType =
			layout == VK_IMAGE_LAYOUT_GENERAL ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		PushDescriptor(
			set,
			binding,
			{
				.sampler     = pSamplerInCharge ? pSamplerInCharge->vkSampler() : VK_NULL_HANDLE,
				.imageView   = pTexture->vkView(),
				.imageLayout = layout
			}, descType);
	}
	else if (IsComputeContext())
	{
		auto [set, binding] = m_pComputePipeline->GetResourceBindingIndex(name);
		if (set == INVALID_INDEX || binding == INVALID_INDEX)
		{
			__debugbreak();
		}

		auto layout = pTexture->GetState().GetSubresourceState().layout;

		VkDescriptorType descType =
			layout == VK_IMAGE_LAYOUT_GENERAL ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		PushDescriptor(
			set,
			binding,
			{
				.sampler     = pSamplerInCharge ? pSamplerInCharge->vkSampler() : VK_NULL_HANDLE,
				.imageView   = pTexture->vkView(),
				.imageLayout = layout
			}, descType);
	}
	else
	{
		__debugbreak();
		assert(false && "No pipeline is set!");
	}
}

void VkCommandContext::Impl::StageDescriptor(const std::string& name, Arc< VulkanBuffer > pBuffer, u32 offset)
{
	if (IsGraphicsContext())
	{
		auto [set, binding] = m_pGraphicsPipeline->GetResourceBindingIndex(name);

		PushDescriptor(
			set,
			binding,
			{
				.buffer = pBuffer->vkBuffer(),
				.offset = offset,
				.range  = pBuffer->SizeInBytes()
			}, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	}
	else if (IsComputeContext())
	{
		auto [set, binding] = m_pComputePipeline->GetResourceBindingIndex(name);

		PushDescriptor(
			set,
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

void VkCommandContext::Impl::PushDescriptor(u32 set, u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType)
{
	m_PushAllocations[set].push_back({ binding, imageInfo, descriptorType });
}

void VkCommandContext::Impl::PushDescriptor(u32 set, u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType)
{
	m_PushAllocations[set].push_back({ binding, bufferInfo, descriptorType });
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
	BindShaderResources(VK_PIPELINE_BIND_POINT_GRAPHICS, m_pGraphicsPipeline->vkPipelineLayout());

	vkCmdDraw(m_vkCommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VkCommandContext::Impl::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
	FlushBarriers();
	BindShaderResources(VK_PIPELINE_BIND_POINT_GRAPHICS, m_pGraphicsPipeline->vkPipelineLayout());

	vkCmdDrawIndexed(m_vkCommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VkCommandContext::Impl::DrawMeshTasksIndirect(const Arc< VulkanBuffer >& pArgumentBuffer, u64 offsetInBytes, u32 numDraws, u32 strideInBytes)
{
	if (!vkCmdDrawMeshTasksIndirectEXT)
		vkCmdDrawMeshTasksIndirectEXT = (PFN_vkCmdDrawMeshTasksIndirectEXT)vkGetInstanceProcAddr(m_RenderDevice.vkInstance(), "vkCmdDrawMeshTasksIndirectEXT");

	FlushBarriers();
	BindShaderResources(VK_PIPELINE_BIND_POINT_GRAPHICS, m_pGraphicsPipeline->vkPipelineLayout());

	vkCmdDrawMeshTasksIndirectEXT(m_vkCommandBuffer, pArgumentBuffer->vkBuffer(), offsetInBytes, numDraws, strideInBytes);
}

void VkCommandContext::Impl::DrawMeshTasksIndirectCount(const Arc< VulkanBuffer >& pArgumentBuffer, u64 offsetInBytes, const Arc< VulkanBuffer >& pCountBuffer, u32 numDraws, u32 strideInBytes)
{
	if (!vkCmdDrawMeshTasksIndirectCountEXT)
		vkCmdDrawMeshTasksIndirectCountEXT = (PFN_vkCmdDrawMeshTasksIndirectCountEXT)vkGetInstanceProcAddr(m_RenderDevice.vkInstance(), "vkCmdDrawMeshTasksIndirectCountEXT");

	FlushBarriers();
	BindShaderResources(VK_PIPELINE_BIND_POINT_GRAPHICS, m_pGraphicsPipeline->vkPipelineLayout());

	vkCmdDrawMeshTasksIndirectCountEXT(m_vkCommandBuffer, pArgumentBuffer->vkBuffer(), offsetInBytes, pCountBuffer->vkBuffer(), 0, numDraws, strideInBytes);
}

void VkCommandContext::Impl::Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	FlushBarriers();
	BindShaderResources(VK_PIPELINE_BIND_POINT_COMPUTE, m_pComputePipeline->vkPipelineLayout());

	vkCmdDispatch(m_vkCommandBuffer, numGroupsX, numGroupsY, numGroupsZ);
}

double VkCommandContext::Impl::GetElapsedTime() const
{
	return m_LastFrameElapsedTime;
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

void VkCommandContext::Impl::BindShaderResources(VkPipelineBindPoint bindPoint, VkPipelineLayout vkPipelineLayout)
{
	if (m_bTransient == false)
	{
		auto& rm = m_RenderDevice.GetResourceManager();
		rm.GetSceneResource().BindSceneResources(m_CommandContext);
	}

	for (const auto& [set, allocations] : m_PushAllocations)
	{
		std::vector< VkWriteDescriptorSet > writes; writes.reserve(allocations.size());
		for (const auto& allocation : allocations)
		{
			VkWriteDescriptorSet write = {};
			write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet          = VK_NULL_HANDLE;
			write.dstBinding      = allocation.binding;
			write.dstArrayElement = 0;
			write.descriptorCount = 1;
			write.descriptorType  = allocation.descriptorType;
			if (allocation.descriptor.bImage)
				write.pImageInfo  = &allocation.descriptor.imageInfo;
			else
				write.pBufferInfo = &allocation.descriptor.bufferInfo;
			writes.push_back(write);
		}

		vkCmdPushDescriptorSetKHR(
			m_vkCommandBuffer,
			bindPoint,
			vkPipelineLayout,
			set, static_cast<u32>(writes.size()), writes.data());
	}
}


//-------------------------------------------------------------------------
// Command Context
//-------------------------------------------------------------------------
VkCommandContext::VkCommandContext(VkRenderDevice& rd, VkCommandPool vkCommandPool, eCommandType type, VkCommandBufferLevel level)
	: m_Impl(MakeBox< Impl >(rd, *this, vkCommandPool, type, level)) {}

void VkCommandContext::Open(VkCommandBufferUsageFlags flags)
{
	m_Impl->Open(flags);
}

void VkCommandContext::Close()
{
	m_Impl->Close();
}

void VkCommandContext::UploadData(const Arc< render::Buffer >& pDstBuffer, const void* pData, u32 numElements, u64 elemSizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes)
{
	m_Impl->UploadData(StaticCast<VulkanBuffer>(pDstBuffer), pData, numElements, elemSizeInBytes, dstStageMask, dstOffsetInBytes);
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
	const Arc< VulkanBuffer >& dstBuffer,
	const Arc< VulkanBuffer >& srcBuffer,
	VkDeviceSize sizeInBytes,
	VkPipelineStageFlags2 dstStageMask,
	VkDeviceSize dstOffset,
	VkDeviceSize srcOffset,
	bool bFlushImmediate)
{
	m_Impl->CopyBuffer(dstBuffer, srcBuffer, sizeInBytes, dstStageMask, dstOffset, srcOffset, bFlushImmediate);
}

void VkCommandContext::CopyBuffer(
	const Arc< VulkanTexture >& dstTexture,
	const Arc< VulkanBuffer >& srcBuffer, 
	const std::vector< VkBufferImageCopy >& regions, 
	bool bAllSubresources)
{
	m_Impl->CopyBuffer(dstTexture, srcBuffer, regions, bAllSubresources);
}

void VkCommandContext::CopyBuffer(const Arc< render::Buffer >& pDstBuffer, const Arc< render::Buffer >& pSrcBuffer, u64 dstOffsetInBytes, u64 srcOffsetInBytes)
{
	auto rhiBufferDst = StaticCast<VulkanBuffer>(pDstBuffer);
	auto rhiBufferSrc = StaticCast<VulkanBuffer>(pSrcBuffer);
	assert(rhiBufferDst && rhiBufferSrc);

	m_Impl->CopyBuffer(rhiBufferDst, rhiBufferSrc, (VkDeviceSize)pSrcBuffer->SizeInBytes(), VK_PIPELINE_STAGE_2_TRANSFER_BIT, dstOffsetInBytes, srcOffsetInBytes);
}

void VkCommandContext::CopyTexture(const Arc< render::Texture >& pDstTexture, const Arc< render::Texture >& pSrcTexture, u64 offsetInBytes)
{
	UNUSED(offsetInBytes);

	auto rhiTextureDst = StaticCast<VulkanTexture>(pDstTexture);
	auto rhiTextureSrc = StaticCast<VulkanTexture>(pSrcTexture);
	assert(rhiTextureDst && rhiTextureSrc);

	m_Impl->CopyTexture(rhiTextureDst, rhiTextureSrc);
}

void VkCommandContext::BlitTexture(Arc< VulkanTexture > dstTexture, Arc< VulkanTexture > srcTexture)
{
	m_Impl->BlitTexture(dstTexture, srcTexture);
}

void VkCommandContext::GenerateMips(Arc< VulkanTexture > texture)
{
	m_Impl->GenerateMips(texture);
}

void VkCommandContext::TransitionBufferToRead(const Arc< render::Buffer >& pBuffer, render::ePipelineStage dstStage, u64 offsetInBytes, bool bFlushImmediate)
{
	auto rhiResource = StaticCast<VulkanBuffer>(pBuffer);
	assert(rhiResource);

	auto vkDstStage = VK_PIPELINE_STAGE2(dstStage);

	auto vkDstAccessFlags = VK_ACCESS_2_SHADER_READ_BIT;
	if (vkDstStage == VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT)
	{
		vkDstAccessFlags = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
	}

	BarrierState barrier = BarrierState(vkDstAccessFlags, vkDstStage);
	m_Impl->TransitionBarrier(
		rhiResource, 
		barrier,
		offsetInBytes, 
		bFlushImmediate);
}

void VkCommandContext::TransitionBufferToWrite(const Arc< render::Buffer >& pBuffer, render::ePipelineStage dstStage, u64 offsetInBytes, bool bFlushImmediate)
{
	auto rhiResource = StaticCast<VulkanBuffer>(pBuffer);
	assert(rhiResource);

	auto vkDstStage = VK_PIPELINE_STAGE2(dstStage);

	BarrierState barrier = BarrierState(VK_ACCESS_2_SHADER_WRITE_BIT, vkDstStage);
	m_Impl->TransitionBarrier(
		rhiResource,
		barrier,
		offsetInBytes, 
		bFlushImmediate);
}

void VkCommandContext::TransitionBarrier(const Arc< render::Texture >& texture, render::eTextureLayout newState, u32 subresource, bool bFlushImmediate)
{
	UNUSED(subresource);

	auto rhiResource = StaticCast<VulkanTexture>(texture);
	assert(rhiResource);

	TransitionImageLayout(rhiResource, VK_LAYOUT(newState), texture->IsDepthTexture() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, bFlushImmediate);
}

void VkCommandContext::UAVBarrier(const Arc< render::Buffer >& pBuffer, bool bFlushImmediate)
{
	m_Impl->UAVBarrier(StaticCast<VulkanBuffer>(pBuffer), bFlushImmediate);
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

void VkCommandContext::ClearBuffer(const Arc< render::Buffer >& pBuffer, u32 value, u64 offsetInBytes)
{
	m_Impl->FillBuffer(StaticCast<VulkanBuffer>(pBuffer), value, offsetInBytes);
}

void VkCommandContext::ClearTexture(const Arc< render::Texture >& pTexture, render::eTextureLayout newLayout)
{
	ClearTexture(StaticCast<VulkanTexture>(pTexture), VK_LAYOUT(newLayout));
}

void VkCommandContext::ClearTexture(
	const Arc< VulkanTexture >& texture,
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

void VkCommandContext::SetAccelerationStructure(const std::string& name, render::TopLevelAccelerationStructure& tlas)
{
	// TODO
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

void VkCommandContext::PushDescriptor(u32 set, u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType)
{
	m_Impl->PushDescriptor(set, binding, imageInfo, descriptorType);
}

void VkCommandContext::PushDescriptor(u32 set, u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType)
{
	m_Impl->PushDescriptor(set, binding, bufferInfo, descriptorType);
}

void VkCommandContext::SetRenderPipeline(render::GraphicsPipeline* pRenderPipeline)
{
	auto vkRenderPipeline = static_cast<VulkanGraphicsPipeline*>(pRenderPipeline);
	assert(vkRenderPipeline);

	m_Impl->SetRenderPipeline(vkRenderPipeline);
}

void VkCommandContext::SetRenderPipeline(render::RaytracingPipeline* pRenderPipeline)
{
	// TODO
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

void VkCommandContext::BuildBLAS(render::BottomLevelAccelerationStructure& blas)
{
	// TODO
}

void VkCommandContext::BuildTLAS(render::TopLevelAccelerationStructure& tlas)
{
	// TODO
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

void VkCommandContext::DrawMeshTasksIndirect(const Arc< render::Buffer >& pArgumentBuffer, u64 offsetInBytes, u32 numDraws, u32 strideInBytes)
{
	const auto& rhiArgBuffer = StaticCast< VulkanBuffer >(pArgumentBuffer);

	m_Impl->DrawMeshTasksIndirect(rhiArgBuffer, offsetInBytes, numDraws, strideInBytes);
}

void VkCommandContext::DrawMeshTasksIndirectCount(const Arc< render::Buffer >& pArgumentBuffer, u64 offsetInBytes, const Arc< render::Buffer >& pCountBuffer, u32 numDraws, u32 strideInBytes)
{
	const auto& rhiArgBuffer   = StaticCast< VulkanBuffer >(pArgumentBuffer);
	const auto& rhiCountBuffer = StaticCast< VulkanBuffer >(pCountBuffer);

	m_Impl->DrawMeshTasksIndirectCount(rhiArgBuffer, offsetInBytes, rhiCountBuffer, numDraws, strideInBytes);
}

void VkCommandContext::Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	m_Impl->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void VkCommandContext::DispatchRays(render::ShaderBindingTable& sbt, u32 width, u32 height, u32 depth)
{
	// TODO
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

void VkCommandContext::FlushBarriers() const
{
	m_Impl->FlushBarriers();
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

double VkCommandContext::GetLastFrameElapsedTime() const
{
	return m_Impl->GetElapsedTime();
}

} // namespace vk