#pragma once

namespace vk
{

class SwapChain;

class FrameManager
{
public:
    FrameManager(VkRenderDevice& rd, SwapChain& swapChain);
    ~FrameManager() = default;

    struct FrameContext
	{
        u32                     imageIndex;
        u32                     contextIndex;
        Arc< VkCommandContext > rhiCommandContext;
    };

    FrameContext BeginFrame();
    void EndFrame(Arc< VkCommandContext >&& pContext);

private:
    VkRenderDevice& m_RenderDevice;
    SwapChain&      m_SwapChain;

    u32 m_ContextIndex = 0;
};

} // namespace vk
