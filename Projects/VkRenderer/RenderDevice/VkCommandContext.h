#pragma once
#include "VkRenderPipeline.h"
#include "RenderResource/VkTexture.h"

namespace vk
{

class RenderTarget;
class DescriptorPool;
class GraphicsPipeline;
class ComputePipeline;
class DescriptorInfo;
class DynamicBufferAllocator;
class StaticBufferAllocator;
class Buffer;

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
class CommandContext
{
public:
    CommandContext(RenderDevice& device, VkCommandPool vkCommandPool, eCommandType type, VkCommandBufferLevel vkLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    virtual ~CommandContext();

    void Open(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
    void Close();
    void Execute();

    void CopyBuffer(
        VkBuffer vkDstBuffer, 
        VkBuffer vkSrcBuffer, 
        VkDeviceSize sizeInBytes, 
        VkPipelineStageFlags2 dstStageMask,
        VkDeviceSize dstOffset = 0, 
        VkDeviceSize srcOffset = 0, 
        bool bFlushImmediate = true);
    void CopyBuffer(
        Arc< Buffer > pDstBuffer,
        Arc< Buffer > pSrcBuffer,
        VkDeviceSize sizeInBytes,
        VkPipelineStageFlags2 dstStageMask,
        VkDeviceSize dstOffset = 0, 
        VkDeviceSize srcOffset = 0, 
        bool bFlushImmediate = true);
    void CopyBuffer(
        Arc< Texture > pDstTexture,
        Arc< Buffer > pSrcBuffer,
        const std::vector< VkBufferImageCopy >& regions, 
        bool bAllSubresources = true);
    void CopyTexture(Arc< Texture > pDstTexture, Arc< Texture > pSrcTexture);
    void BlitTexture(Arc< Texture > pDstTexture, Arc< Texture > pSrcTexture);
    void GenerateMips(Arc< Texture > pTexture);

    // todo. unlock other types of barrier
    void TransitionImageLayout(
        Arc< Texture > pTexture,
        VkImageLayout newLayout,
        VkPipelineStageFlags2 dstStageMask,
        VkImageAspectFlags aspectMask,
        bool bFlushImmediate = true,
        bool bFlatten = false);
    void TransitionImageLayout(
        Arc< Texture > pTexture,
        VkImageLayout newLayout, 
        VkPipelineStageFlags2 dstStageMask,
        VkImageSubresourceRange subresourceRange,
        bool bFlushImmediate = true, 
        bool bFlatten = false);

    void ClearTexture(
        Arc< Texture >pTexture, 
        VkImageLayout newLayout, 
        VkPipelineStageFlags2 dstStageMask,
        u32 baseMip = 0, u32 numMips = 1, u32 baseArray = 0, u32 numArrays = 1);
    
    void SetPushConstants(u32 sizeInBytes, const void* data, VkShaderStageFlags stages, u32 offsetInBytes = 0);
    void SetDynamicUniformBuffer(u32 binding, VkDeviceSize sizeInBytes, const void* bufferData);
    template< typename T >
    void SetDynamicUniformBuffer(u32 binding, const T& data)
    {
        SetDynamicUniformBuffer(binding, sizeof(T), &data);
    }
    void PushDescriptors(u32 binding, const VkDescriptorImageInfo& imageInfo, VkDescriptorType descriptorType);
    void PushDescriptors(u32 binding, const VkDescriptorBufferInfo& bufferInfo, VkDescriptorType descriptorType);

    void BindSceneDescriptors(const SceneResource& sceneResource);

    void SetRenderPipeline(GraphicsPipeline* pRenderPipeline);
    void SetRenderPipeline(ComputePipeline* pRenderPipeline);

    void BeginRenderPass(const RenderTarget& renderTarget);
    void EndRenderPass();
    void BeginRendering(const VkRenderingInfo& renderInfo);
    void EndRendering();

    void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0);
    void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0);
    void DrawIndexedIndirect(const SceneResource& sceneResource);
    
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

    [[nodiscard]]
    bool IsTransient() const { return m_bTransient; }
    void SetTransient(bool bTransient) { m_bTransient = bTransient; }

    [[nodiscard]]
    VkCommandBuffer vkCommandBuffer() const { return m_vkCommandBuffer; }
    [[nodiscard]]
    VkFence vkRenderCompleteFence() const { return m_vkRenderCompleteFence; }
    [[nodiscard]]
    VkSemaphore vkRenderCompleteSemaphore() const { return m_vkRenderCompleteSemaphore; }
    [[nodiscard]]
    VkFence vkPresentCompleteFence() const { return m_vkPresentCompleteFence; }
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

    template< typename T >
    constexpr T RoundUpAndDivide(T Value, size_t Alignment)
    {
        return (T)((Value + Alignment - 1) / Alignment);
    }

private:
    friend class CommandQueue;
    RenderDevice& m_RenderDevice;
    eCommandType  m_CommandType;

    VkCommandBuffer      m_vkCommandBuffer = VK_NULL_HANDLE;
    VkCommandPool        m_vkBelongedPool  = VK_NULL_HANDLE;
    VkCommandBufferLevel m_Level           = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    DynamicBufferAllocator* m_pUniformBufferPool = nullptr;

    VkFence     m_vkRenderCompleteFence      = VK_NULL_HANDLE;
    VkSemaphore m_vkRenderCompleteSemaphore  = VK_NULL_HANDLE;
    VkFence     m_vkPresentCompleteFence     = VK_NULL_HANDLE;
    VkSemaphore m_vkPresentCompleteSemaphore = VK_NULL_HANDLE;

    GraphicsPipeline* m_pGraphicsPipeline = nullptr;
    ComputePipeline*  m_pComputePipeline  = nullptr;

    struct AllocationInfo
    {
        u32              binding;
        DescriptorInfo   descriptor;
        VkDescriptorType descriptorType;
    };
    std::vector< AllocationInfo > m_PushAllocations;

    u32                    m_NumBufferBarriersToFlush                 = 0;
    VkBufferMemoryBarrier2 m_BufferBarriers[MAX_NUM_PENDING_BARRIERS] = {};
    u32                    m_NumImageBarriersToFlush                  = 0;
    VkImageMemoryBarrier2  m_ImageBarriers[MAX_NUM_PENDING_BARRIERS]  = {};

    u32 m_CurrentContextIndex = 0;

    bool m_bTransient = false;
};

} // namespace vk