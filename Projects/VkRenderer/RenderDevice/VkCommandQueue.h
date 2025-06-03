#pragma once

namespace vk
{

class CommandContext;

class CommandQueue
{
public:
	CommandQueue(RenderDevice& device, u32 queueIndex, eCommandType type);
	~CommandQueue();
	
	[[nodiscard]]
	CommandContext& Allocate(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, bool bTransient = false);
	void Flush();

	void ExecuteCommandBuffer(CommandContext& context);


	[[nodiscard]]
	inline u32 Index() const { return m_QueueIndex; }
	[[nodiscard]]
	inline VkQueue vkQueue() const { return m_vkQueue; }
	[[nodiscard]]
	inline VkCommandPool vkCommandPool() const { return m_vkCommandPool; }

private:
	RenderDevice& m_RenderDevice;
	eCommandType  m_CommandType;

	VkQueue       m_vkQueue = VK_NULL_HANDLE;
	VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;

	std::vector< CommandContext* > m_pContexts;
	std::queue< CommandContext* >  m_pAvailableContexts;

	u32 m_QueueIndex = UINT_MAX;
};

} // namespace vk