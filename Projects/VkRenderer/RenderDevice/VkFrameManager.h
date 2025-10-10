#pragma once

namespace vk
{

class SwapChain;

class FrameManager
{
public:
    FrameManager(VkRenderDevice& rd, SwapChain& swapChain);
    ~FrameManager();

    struct FrameContext
	{
        u32                     imageIndex;
        Arc< VkCommandContext > rhiCommandContext;
    };

    FrameContext BeginFrame();

    void EndFrame(Arc< VkCommandContext >&& pContext);

    void WaitIdle();

private:
    VkRenderDevice& m_RenderDevice;
    SwapChain&      m_SwapChain;

    // Per-frame resources
    struct FrameData
	{
        VkFence vkAcquireFence = VK_NULL_HANDLE;
        VkFence vkPresentFence = VK_NULL_HANDLE;

        bool bProcessing = false;
    };
    std::array< FrameData, MAX_FRAMES_IN_FLIGHT > m_Frames;

    u32 m_ContextIndex = 0;
};

} // namespace vk