#include "RendererPch.h"
#include "VkFrameManager.h"
#include "VkSwapChain.h"
#include "VkCommandQueue.h"
#include "VkCommandContext.h"

namespace vk
{

FrameManager::FrameManager(VkRenderDevice& rd, SwapChain& swapChain)
    : m_RenderDevice(rd)
    , m_SwapChain(swapChain)
{
}

FrameManager::FrameContext FrameManager::BeginFrame()
{
    FrameContext context = {};
    auto pContext = m_RenderDevice.GraphicsQueue().Reserve();
    context.imageIndex = m_SwapChain.AcquireNextImage(pContext->vkPresentCompleteSemaphore());
    if (context.imageIndex == kInvalidIndex)
    {
        m_RenderDevice.GraphicsQueue().RecycleUnsubmitted(std::move(pContext));
        return context;
    }

    pContext->Open();
    pContext->SetPresentWaitSemaphore(m_SwapChain.PresentWaitSemaphore(context.imageIndex));
    context.rhiCommandContext = std::move(pContext);
    context.contextIndex      = m_ContextIndex;

    return context;
}

void FrameManager::EndFrame(Arc< VkCommandContext >&& pContext)
{
    m_RenderDevice.ExecuteCommand(pContext);

    // Transient command context is released right after execution
    if (pContext)
        m_SwapChain.Present(pContext->vkRenderCompleteSemaphore());

    m_ContextIndex = (m_ContextIndex + 1) % kMaxFramesInFlight;
}

} // namespace vk
