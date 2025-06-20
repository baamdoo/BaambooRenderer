#include "RendererPch.h"
#include "VkCommandQueue.h"
#include "VkCommandContext.h"

namespace vk
{

CommandQueue::CommandQueue(RenderDevice& device, u32 queueIndex, eCommandType type)
	: m_RenderDevice(device)
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
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; /* reset command buffers individually */
	commandPoolInfo.queueFamilyIndex = m_QueueIndex;
	VK_CHECK(vkCreateCommandPool(m_RenderDevice.vkDevice(), &commandPoolInfo, nullptr, &m_vkCommandPool));
}

CommandQueue::~CommandQueue()
{
	for (auto pContext : m_pContexts)
		RELEASE(pContext);

	vkDestroyCommandPool(m_RenderDevice.vkDevice(), m_vkCommandPool, nullptr);
}

CommandContext& CommandQueue::Allocate(VkCommandBufferUsageFlags flags, bool bTransient)
{
	CommandContext* pContext = nullptr;
	if (bTransient)
	{
		pContext = new CommandContext(m_RenderDevice, m_vkCommandPool, m_CommandType);
	}
	else
	{
		if (!m_pAvailableContexts.empty() && m_pAvailableContexts.front()->IsReady())
		{
			pContext = m_pAvailableContexts.front();
			m_pAvailableContexts.pop();
		}
		else if (m_pContexts.size() == MAX_FRAMES_IN_FLIGHT) // limit the number of commands to avoid ImGui buffer recreate validation
		{
			pContext = m_pAvailableContexts.front();
			m_pAvailableContexts.pop();

			pContext->Flush();
		}
		else
		{
			pContext = new CommandContext(m_RenderDevice, m_vkCommandPool, m_CommandType);
			m_pContexts.push_back(pContext);
		}
	}
	assert(pContext);

	pContext->Open(flags);
	pContext->SetTransient(bTransient);
	return *pContext;
}

void CommandQueue::Flush()
{
	for (auto pContext : m_pContexts)
		pContext->Flush();
}

void CommandQueue::ExecuteCommandBuffer(CommandContext& context)
{
	// **
	// Submit queue
	// **
	if (!context.IsTransient())
	{
		VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submitInfo         = {};
		submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount   = 1;
		submitInfo.pWaitSemaphores      = &context.m_vkPresentCompleteSemaphore;
		submitInfo.pWaitDstStageMask    = &waitStages;
		submitInfo.commandBufferCount   = 1;
		submitInfo.pCommandBuffers      = &context.m_vkCommandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores    = &context.m_vkRenderCompleteSemaphore;
		VK_CHECK(vkQueueSubmit(m_vkQueue, 1, &submitInfo, context.vkRenderCompleteFence()));

		m_pAvailableContexts.push(&context);
	}
	else
	{
		VkSubmitInfo submitInfo       = {};
		submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers    = &context.m_vkCommandBuffer;
		VK_CHECK(vkQueueSubmit(m_vkQueue, 1, &submitInfo, context.vkRenderCompleteFence()));
		context.WaitForFence(context.vkRenderCompleteFence());
		//VK_CHECK(vkQueueWaitIdle(m_vkQueue));

		auto pContext = &context;
		RELEASE(pContext);
	}
}

} // namespace vk