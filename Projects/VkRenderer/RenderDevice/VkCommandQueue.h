#pragma once

namespace vk
{

class CommandBuffer;

class CommandQueue
{
public:
	CommandQueue(RenderContext& context, u32 queueIndex);
	~CommandQueue();
	
	[[nodiscard]]
	CommandBuffer& Allocate(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, bool bTransient = false);
	void Flush();

	void ExecuteCommandBuffer(CommandBuffer& cmdBuffer);


	[[nodiscard]]
	inline u32 Index() const { return m_QueueIndex; }
	[[nodiscard]]
	inline VkQueue vkQueue() const { return m_vkQueue; }
	[[nodiscard]]
	inline VkCommandPool vkCommandPool() const { return m_vkCommandPool; }

private:
	CommandBuffer* RequestList(VkCommandPool vkCommandPool);

private:
	RenderContext& m_RenderContext;

	VkQueue       m_vkQueue = VK_NULL_HANDLE;
	VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;

	std::vector< CommandBuffer* > m_pCmdBuffers;
	std::queue< CommandBuffer* >  m_pAvailableCmdBuffers;

	u32 m_QueueIndex = UINT_MAX;
};

} // namespace vk