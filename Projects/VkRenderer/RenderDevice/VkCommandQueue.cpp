#include "RendererPch.h"
#include "VkCommandQueue.h"
#include "VkCommandBuffer.h"

namespace vk
{

CommandQueue::CommandQueue(RenderContext& context, u32 queueIndex)
	: m_RenderContext(context)
	, m_QueueIndex(queueIndex)
{
	// **
	// Get queue
	// **
	vkGetDeviceQueue(m_RenderContext.vkDevice(), m_QueueIndex, 0, &m_vkQueue);
	assert(m_vkQueue);


	// **
	// Create command pool
	// **
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; /* reset command buffers individually */
	commandPoolInfo.queueFamilyIndex = m_QueueIndex;
	VK_CHECK(vkCreateCommandPool(m_RenderContext.vkDevice(), &commandPoolInfo, nullptr, &m_vkCommandPool));
}

CommandQueue::~CommandQueue()
{
	for (auto pCmdBuffer : m_pCmdBuffers)
		RELEASE(pCmdBuffer);

	vkDestroyCommandPool(m_RenderContext.vkDevice(), m_vkCommandPool, nullptr);
}

CommandBuffer& CommandQueue::Allocate(VkCommandBufferUsageFlags flags, bool bTransient)
{
	CommandBuffer* pCmdBuffer = nullptr;
	if (bTransient)
	{
		pCmdBuffer = new CommandBuffer(m_RenderContext, m_vkCommandPool);
	}
	else
	{
		if (!m_pAvailableCmdBuffers.empty() && m_pAvailableCmdBuffers.front()->IsFenceComplete())
		{
			pCmdBuffer = m_pAvailableCmdBuffers.front();
			m_pAvailableCmdBuffers.pop();
		}
		else
		{
			pCmdBuffer = new CommandBuffer(m_RenderContext, m_vkCommandPool);
			m_pCmdBuffers.push_back(pCmdBuffer);
		}
	}
	assert(pCmdBuffer);

	pCmdBuffer->Open(flags);
	pCmdBuffer->SetTransient(bTransient);
	return *pCmdBuffer;
}

void CommandQueue::Flush()
{
	for (auto pCmdBuffer : m_pCmdBuffers)
		pCmdBuffer->WaitForFence();
}

void CommandQueue::ExecuteCommandBuffer(CommandBuffer& cmdBuffer)
{
	// **
	// Submit queue
	// **
	if (!cmdBuffer.IsTransient())
	{
		VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &cmdBuffer.m_vkPresentCompleteSemaphore;
		submitInfo.pWaitDstStageMask = &waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdBuffer.m_vkCommandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &cmdBuffer.m_vkRenderCompleteSemaphore;
		VK_CHECK(vkQueueSubmit(m_vkQueue, 1, &submitInfo, cmdBuffer.m_vkFence));

		m_pAvailableCmdBuffers.push(&cmdBuffer);
	}
	else
	{
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdBuffer.m_vkCommandBuffer;
		VK_CHECK(vkQueueSubmit(m_vkQueue, 1, &submitInfo, cmdBuffer.m_vkFence));
		cmdBuffer.WaitForFence();
		//VK_CHECK(vkQueueWaitIdle(m_vkQueue));

		auto pCmdBuffer = &cmdBuffer;
		RELEASE(pCmdBuffer);
	}
}

CommandBuffer* CommandQueue::RequestList(VkCommandPool vkCommandPool)
{
	auto pCommandBuffer = new CommandBuffer(m_RenderContext, vkCommandPool);
	return pCommandBuffer;
}

} // namespace vk