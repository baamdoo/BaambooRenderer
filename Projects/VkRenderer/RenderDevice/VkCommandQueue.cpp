#include "RendererPch.h"
#include "VkCommandQueue.h"
#include "VkCommandBuffer.h"

namespace vk
{

CommandQueue::CommandQueue(RenderContext& context, u32 queueIndex)
	: m_renderContext(context)
	, m_queueIndex(queueIndex)
{
	// **
	// Get queue
	// **
	vkGetDeviceQueue(m_renderContext.vkDevice(), m_queueIndex, 0, &m_vkQueue);
	assert(m_vkQueue);


	// **
	// Create command pool
	// **
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; /* reset command buffers individually */
	commandPoolInfo.queueFamilyIndex = m_queueIndex;
	VK_CHECK(vkCreateCommandPool(m_renderContext.vkDevice(), &commandPoolInfo, nullptr, &m_vkCommandPool));
}

CommandQueue::~CommandQueue()
{
	for (auto pCmdBuffer : m_pCmdBuffers)
		RELEASE(pCmdBuffer);

	vkDestroyCommandPool(m_renderContext.vkDevice(), m_vkCommandPool, nullptr);
}

CommandBuffer& CommandQueue::Allocate()
{
	CommandBuffer* pCmdBuffer = nullptr;
	if (!m_pAvailableCmdBuffers.empty() && m_pAvailableCmdBuffers.front()->IsFenceComplete())
	{
		pCmdBuffer = m_pAvailableCmdBuffers.front();
		m_pAvailableCmdBuffers.pop();
	}
	else
	{
		pCmdBuffer = new CommandBuffer(m_renderContext, m_vkCommandPool);
		m_pCmdBuffers.push_back(pCmdBuffer);
	}
	assert(pCmdBuffer);

	pCmdBuffer->Open();
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

CommandBuffer* CommandQueue::RequestList(VkCommandPool vkCommandPool)
{
	auto pCommandBuffer = new CommandBuffer(m_renderContext, vkCommandPool);
	return pCommandBuffer;
}

} // namespace vk