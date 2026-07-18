#include "RendererPch.h"
#include "VkCommandQueue.h"
#include "VkCommandContext.h"

namespace vk
{

CommandQueue::CommandQueue(VkRenderDevice& rd, u32 queueIndex, eCommandType type)
	: m_RenderDevice(rd)
	, m_CommandType(type)
	, m_QueueIndex(queueIndex)
{
	// **
	// Get queue
	// **
	vkGetDeviceQueue(m_RenderDevice.vkDevice(), m_QueueIndex, 0, &m_vkQueue);
	assert(m_vkQueue);
	

	// **
	// Create command pool
	// **
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; /* reset command buffers individually */
	commandPoolInfo.queueFamilyIndex = m_QueueIndex;
	VK_CHECK(vkCreateCommandPool(m_RenderDevice.vkDevice(), &commandPoolInfo, nullptr, &m_vkCommandPool));
}

CommandQueue::~CommandQueue()
{
	// explicit release due to release dependency
	while (!m_pAvailableContexts.empty())
	{
		m_pAvailableContexts.pop();
	}
	m_pContexts.clear();

	vkDestroyCommandPool(m_RenderDevice.vkDevice(), m_vkCommandPool, nullptr);
}

Arc< VkCommandContext > CommandQueue::Allocate(VkCommandBufferUsageFlags flags, bool bTransient)
{
	auto pContext = Reserve(bTransient);
	pContext->Open(flags);
	return pContext;
}

Arc< VkCommandContext > CommandQueue::Reserve(bool bTransient)
{
	Arc< VkCommandContext > pContext;
	if (bTransient)
	{
		pContext = MakeArc< VkCommandContext >(m_RenderDevice, m_vkCommandPool, m_CommandType);
	}
	else
	{
		if (!m_pAvailableContexts.empty() && m_pAvailableContexts.front()->IsReady())
		{
			pContext = m_pAvailableContexts.front();
			m_pAvailableContexts.pop();
		}
		else if (m_pContexts.size() == kMaxFramesInFlight) // limit the number of commands to avoid ImGui buffer recreate validation
		{
			BB_ASSERT(!m_pAvailableContexts.empty(), "No Vulkan command context is available for reuse.");
			pContext = m_pAvailableContexts.front();
			m_pAvailableContexts.pop();
			pContext->Flush();
		}
		else
		{
			pContext = MakeArc< VkCommandContext >(m_RenderDevice, m_vkCommandPool, m_CommandType);
			m_pContexts.push_back(pContext);
		}
	}
	assert(pContext);

	pContext->SetTransient(bTransient);
	return pContext;
}

void CommandQueue::RecycleUnsubmitted(Arc< VkCommandContext >&& pContext)
{
	BB_ASSERT(pContext && !pContext->IsTransient(),
		"Only a reserved non-transient Vulkan command context can be recycled without submission.");
	m_pAvailableContexts.push(std::move(pContext));
}

void CommandQueue::Flush()
{
	for (auto Context : m_pContexts)
		if (Context)
			Context->Flush();
}

void CommandQueue::ExecuteCommandBuffer(Arc< VkCommandContext > context)
{
	// **
	// Submit queue
	// **
	if (!context->IsTransient())
	{
		VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_TRANSFER_BIT;

		auto vkWaitSemaphore   = context->vkPresentCompleteSemaphore();
		auto vkCommandBuffer   = context->vkCommandBuffer();
		auto vkSignalSemaphore = context->vkRenderCompleteSemaphore();
		auto vkRenderCompleteFence = context->vkRenderCompleteFence();

		VkSubmitInfo submitInfo = {};
		submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount   = 1;
		submitInfo.pWaitSemaphores      = &vkWaitSemaphore;
		submitInfo.pWaitDstStageMask    = &waitStages;
		submitInfo.commandBufferCount   = 1;
		submitInfo.pCommandBuffers      = &vkCommandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores    = &vkSignalSemaphore;
		VK_CHECK(vkResetFences(m_RenderDevice.vkDevice(), 1, &vkRenderCompleteFence));
		VK_CHECK(vkQueueSubmit(m_vkQueue, 1, &submitInfo, vkRenderCompleteFence));

		m_pAvailableContexts.push(context);
	}
	else
	{
		auto vkCommandBuffer = context->vkCommandBuffer();
		auto vkRenderCompleteFence = context->vkRenderCompleteFence();

		VkSubmitInfo submitInfo = {};
		submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers    = &vkCommandBuffer;
		VK_CHECK(vkResetFences(m_RenderDevice.vkDevice(), 1, &vkRenderCompleteFence));
		VK_CHECK(vkQueueSubmit(m_vkQueue, 1, &submitInfo, vkRenderCompleteFence));
		//context->WaitForFence(context->vkRenderCompleteFence());
		VK_CHECK(vkQueueWaitIdle(m_vkQueue));

		context.reset();
	}
}

} // namespace vk