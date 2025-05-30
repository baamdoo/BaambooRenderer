#pragma once
#include "VkRenderPipeline.h"
#include "Core/VkCore.h"

namespace vk
{

class RenderTarget;
class DescriptorPool;
class GraphicsPipeline;
class ComputePipeline;
class DescriptorInfo;
class DynamicBufferAllocator;
class StaticBufferAllocator;
class VertexBuffer;
class IndexBuffer;
class Buffer;
class Texture;

constexpr u32 MAX_NUM_PENDING_BARRIERS = 16;

//-------------------------------------------------------------------------
// CommandBuffer
//-------------------------------------------------------------------------
class CommandBuffer
{
public:
    CommandBuffer(RenderContext& context, VkCommandPool vkCommandPool, VkCommandBufferLevel vkLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    virtual ~CommandBuffer();

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
        Buffer* pDstBuffer, 
        Buffer* pSrcBuffer, 
        VkDeviceSize sizeInBytes,
        VkPipelineStageFlags2 dstStageMask,
        VkDeviceSize dstOffset = 0, 
        VkDeviceSize srcOffset = 0, 
        bool bFlushImmediate = true);
    void CopyBuffer(
        Texture* pDstTexture, 
        Buffer* pSrcBuffer, 
        const std::vector< VkBufferImageCopy >& regions, 
        bool bAllSubresources = true);
    void CopyTexture(Texture* pDstTexture, Texture* pSrcTexture);
    void GenerateMips(Texture* pTexture);

    // todo. unlock other types of barrier
    void TransitionImageLayout(
        Texture* pTexture,
        VkImageLayout newLayout,
        VkPipelineStageFlags2 dstStageMask,
        VkImageAspectFlags aspectMask,
        bool bFlushImmediate = true,
        bool bFlatten = false);
    void TransitionImageLayout(
        Texture* pTexture, 
        VkImageLayout newLayout, 
        VkPipelineStageFlags2 dstStageMask,
        VkImageSubresourceRange subresourceRange,
        bool bFlushImmediate = true, 
        bool bFlatten = false);

    void ClearTexture(
        Texture* pTexture, 
        VkImageLayout newLayout, 
        VkPipelineStageFlags2 dstStageMask,
        u32 baseMip = 0, u32 numMips = 1, u32 baseArray = 0, u32 numArrays = 1);
    
    void SetGraphicsPushConstants(u32 sizeInBytes, void* data, VkShaderStageFlags stages, u32 offsetInBytes = 0);
    void SetGraphicsDynamicUniformBuffer(u32 binding, VkDeviceSize sizeInBytes, const void* bufferData);
    template< typename T >
    void SetGraphicsDynamicUniformBuffer(u32 binding, const T& data)
    {
        SetGraphicsDynamicUniformBuffer(binding, sizeof(T), &data);
    }
    void PushDescriptors(u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType);
    void PushDescriptors(u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType);

    void SetRenderPipeline(GraphicsPipeline* pRenderPipeline);
    void SetRenderPipeline(ComputePipeline* pRenderPipeline);

    void BeginRenderPass(const RenderTarget& renderTarget);
    void EndRenderPass();

    void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0);
    void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0);
    void DrawIndexedIndirect(const SceneResource& sceneResource);

    [[nodiscard]]
    bool IsFenceComplete() const;
    void WaitForFence() const;

    [[nodiscard]]
    bool IsTransient() const { return m_bTransient; }
    void SetTransient(bool bTransient) { m_bTransient = bTransient; }

    [[nodiscard]]
    VkCommandBuffer vkCommandBuffer() const { return m_vkCommandBuffer; }
    [[nodiscard]]
    VkSemaphore vkRenderCompleteSemaphore() const { return m_vkRenderCompleteSemaphore; }
    [[nodiscard]]
    VkSemaphore vkPresentCompleteSemaphore() const { return m_vkPresentCompleteSemaphore; }
    [[nodiscard]]
    VkPipeline vkGraphicsPipeline() const { assert(m_pGraphicsPipeline); return m_pGraphicsPipeline->vkPipeline(); }
    [[nodiscard]]
    VkPipeline vkComputePipeline() const { assert(m_pComputePipeline); return m_pComputePipeline->vkPipeline(); }

private:
    void AddBarrier(const VkBufferMemoryBarrier2& barrier, bool bFlushImmediate);
    void AddBarrier(const VkImageMemoryBarrier2& barrier, bool bFlushImmediate);
    void FlushBarriers();

private:
    friend class CommandQueue;
    RenderContext& m_RenderContext;

    VkCommandBuffer      m_vkCommandBuffer = VK_NULL_HANDLE;
    VkCommandPool        m_vkBelongedPool = VK_NULL_HANDLE;
    VkCommandBufferLevel m_Level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    DynamicBufferAllocator* m_pUniformBufferPool = nullptr;

    VkSemaphore m_vkRenderCompleteSemaphore = VK_NULL_HANDLE;
    VkSemaphore m_vkPresentCompleteSemaphore = VK_NULL_HANDLE;
    VkFence     m_vkFence = VK_NULL_HANDLE;

    GraphicsPipeline* m_pGraphicsPipeline = nullptr;
    ComputePipeline*  m_pComputePipeline = nullptr;

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

} // namespace vk