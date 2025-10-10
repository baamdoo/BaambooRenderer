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
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (auto& frame : m_Frames) 
    {
        VK_CHECK(vkCreateFence(m_RenderDevice.vkDevice(), &fenceInfo, nullptr, &frame.vkAcquireFence));
        VK_CHECK(vkCreateFence(m_RenderDevice.vkDevice(), &fenceInfo, nullptr, &frame.vkPresentFence));
    }
}

FrameManager::~FrameManager()
{
    WaitIdle();

    for (auto& frame : m_Frames) 
    {
        vkDestroyFence(m_RenderDevice.vkDevice(), frame.vkAcquireFence, nullptr);
        vkDestroyFence(m_RenderDevice.vkDevice(), frame.vkPresentFence, nullptr);
    }
}

FrameManager::FrameContext FrameManager::BeginFrame()
{
    auto& frame = m_Frames[m_ContextIndex];

    /*VkFence vkFences[] = { frame.vkAcquireFence, frame.vkPresentFence };
    VK_CHECK(vkWaitForFences(m_RenderDevice.vkDevice(), 2, vkFences, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(m_RenderDevice.vkDevice(), 2, vkFences));*/

    FrameContext context = {};
    context.rhiCommandContext = m_RenderDevice.BeginCommand(eCommandType::Graphics);
    context.imageIndex        = m_SwapChain.AcquireNextImage(context.rhiCommandContext->vkPresentCompleteSemaphore());

    frame.bProcessing = true;

    return context;
}

void FrameManager::EndFrame(Arc< VkCommandContext >&& pContext)
{
    m_RenderDevice.ExecuteCommand(pContext);

    // Transient command context is released right after execution
    if (pContext)
    {
        m_SwapChain.Present(pContext->vkRenderCompleteSemaphore(), pContext->vkPresentCompleteFence());
    }

    m_Frames[m_ContextIndex].bProcessing = false;
    m_ContextIndex = (m_ContextIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void FrameManager::WaitIdle()
{
    for (auto& frame : m_Frames)
    {
        VkFence vkFences[] = { frame.vkAcquireFence, frame.vkPresentFence };
        VK_CHECK(vkWaitForFences(m_RenderDevice.vkDevice(), 2, vkFences, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(m_RenderDevice.vkDevice(), 2, vkFences));
    }
}

} // namespace vk