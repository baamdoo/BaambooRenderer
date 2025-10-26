#pragma once
#include "VkRenderPipeline.h"
#include "RenderResource/VkTexture.h"
#include "RenderCommon/CommandContext.h"

namespace vk
{

class RenderTarget;
class DescriptorPool;
class GraphicsPipeline;
class ComputePipeline;
class DescriptorInfo;
class DynamicBufferAllocator;
class StaticBufferAllocator;
class VulkanBuffer;

constexpr u32 MAX_NUM_PENDING_BARRIERS = 16;

enum class eCommandType
{
    Graphics,
    Compute,
    Transfer,
};

//-------------------------------------------------------------------------
// Command Context
//-------------------------------------------------------------------------
class VkCommandContext : public render::CommandContext
{
public:
    VkCommandContext(VkRenderDevice& rd, VkCommandPool vkCommandPool, eCommandType type, VkCommandBufferLevel vkLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    virtual ~VkCommandContext() = default;

    void Open(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
    void Close();

    virtual void CopyBuffer(Arc< render::Buffer > dstBuffer, Arc< render::Buffer > srcBuffer, u64 offsetInBytes = 0) override;
    virtual void CopyTexture(Arc< render::Texture > dstTexture, Arc< render::Texture > srcTexture, u64 offsetInBytes = 0) override;

	void CopyBuffer(
		VkBuffer vkDstBuffer,
		VkBuffer vkSrcBuffer,
		VkDeviceSize sizeInBytes,
		VkPipelineStageFlags2 dstStageMask,
		VkDeviceSize dstOffset = 0,
		VkDeviceSize srcOffset = 0,
		bool bFlushImmediate = true);
	void CopyBuffer(
		Arc< VulkanBuffer > dstBuffer,
		Arc< VulkanBuffer > srcBuffer,
		VkDeviceSize sizeInBytes,
		VkPipelineStageFlags2 dstStageMask,
		VkDeviceSize dstOffset = 0,
		VkDeviceSize srcOffset = 0,
		bool bFlushImmediate = true);
	void CopyBuffer(
		Arc< VulkanTexture > dstTexture,
		Arc< VulkanBuffer > srcBuffer,
		const std::vector< VkBufferImageCopy >& regions,
		bool bAllSubresources = true);
	void BlitTexture(Arc< VulkanTexture > dstTexture, Arc< VulkanTexture > srcTexture);
	void GenerateMips(Arc< VulkanTexture > texture);

	virtual void TransitionBarrier(Arc< render::Texture > texture, render::eTextureLayout newState, u32 subresource = ALL_SUBRESOURCES, bool bFlushImmediate = false) override;

	// todo. unlock other types of barrier
	void TransitionImageLayout(
		Arc< VulkanTexture > texture,
		VkImageLayout newLayout,
		VkImageAspectFlags aspectMask,
		bool bFlushImmediate = true,
		bool bFlatten = false);
	void TransitionImageLayout(
		Arc< VulkanTexture > texture,
		VkImageLayout newLayout,
		VkImageSubresourceRange subresourceRange,
		bool bFlushImmediate = true,
		bool bFlatten = false);

	void ClearTexture(
		Arc< VulkanTexture > texture,
		VkImageLayout newLayout,
		u32 baseMip = 0, u32 numMips = 1, u32 baseArray = 0, u32 numArrays = 1);

	virtual void SetRenderPipeline(render::ComputePipeline* pPipeline) override;
	virtual void SetRenderPipeline(render::GraphicsPipeline* pPipeline) override;

	virtual void SetComputeConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes = 0) override;
	virtual void SetGraphicsConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes = 0) override;

	virtual void SetComputeDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData) override;
	virtual void SetGraphicsDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData) override;

	virtual void SetComputeShaderResource(const std::string& name, Arc< render::Texture > texture, Arc< render::Sampler > samplerInCharge) override;
	virtual void SetGraphicsShaderResource(const std::string& name, Arc< render::Texture > texture, Arc< render::Sampler > samplerInCharge) override;
	virtual void SetComputeShaderResource(const std::string& name, Arc< render::Buffer > buffer) override;
	virtual void SetGraphicsShaderResource(const std::string& name, Arc< render::Buffer > buffer) override;

	virtual void StageDescriptor(const std::string& name, Arc< render::Buffer > buffer, u32 offset = 0) override;
	virtual void StageDescriptor(const std::string& name, Arc< render::Texture > texture, Arc< render::Sampler > samplerInCharge, u32 offset = 0) override;

	void PushDescriptor(u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType);
	void PushDescriptor(u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType);

	void SetRenderPipeline(GraphicsPipeline* pRenderPipeline);
	void SetRenderPipeline(ComputePipeline* pRenderPipeline);

	virtual void BeginRenderPass(Arc< render::RenderTarget > renderTarget) override;
	virtual void EndRenderPass() override;

	void BeginRendering(const VkRenderingInfo& renderInfo);
	void EndRendering();

	virtual void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) override;
	virtual void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0) override;
	virtual void DrawScene(const render::SceneResource& sceneResource) override;

	virtual void Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ) override;
	// TODO. virtual void DispatchIndirect(Arc< Buffer > argumentBuffer, u32 argumentBufferOffset = 0) override {}

	bool IsReady() const;
	bool IsFenceComplete(VkFence vkFence) const;
	void WaitForFence(VkFence vkFence) const;
	void Flush() const;

	eCommandType GetCommandType() const;

	bool IsTransient() const;
	void SetTransient(bool bTransient);

	bool IsGraphicsContext() const;
	bool IsComputeContext() const;

	VkCommandBuffer vkCommandBuffer() const;

	VkFence vkRenderCompleteFence() const;
	VkSemaphore vkRenderCompleteSemaphore() const;
	VkFence vkPresentCompleteFence() const;
	VkSemaphore vkPresentCompleteSemaphore() const;

	VkPipelineLayout vkGraphicsPipelineLayout() const;
	VkPipelineLayout vkComputePipelineLayout() const;
	VkPipeline vkGraphicsPipeline() const;
	VkPipeline vkComputePipeline() const;

private:
    class Impl;
    Box< Impl > m_Impl;
};

} // namespace vk