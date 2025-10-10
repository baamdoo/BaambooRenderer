#pragma once

namespace vk
{

class VkCommandContext;

class CommandQueue
{
public:
	CommandQueue(VkRenderDevice& rd, u32 queueIndex, eCommandType type);
	~CommandQueue();
	
	[[nodiscard]]
	Arc< VkCommandContext > Allocate(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, bool bTransient = false);
	void Flush();

	void ExecuteCommandBuffer(Arc< VkCommandContext > context);


	[[nodiscard]]
	inline u32 Index() const { return m_QueueIndex; }
	[[nodiscard]]
	inline VkQueue vkQueue() const { return m_vkQueue; }
	[[nodiscard]]
	inline VkCommandPool vkCommandPool() const { return m_vkCommandPool; }

private:
	VkRenderDevice& m_RenderDevice;
	eCommandType    m_CommandType;

	VkQueue       m_vkQueue       = VK_NULL_HANDLE;
	VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;

	std::vector< Arc< VkCommandContext > > m_pContexts;
	std::queue< Arc< VkCommandContext > >  m_pAvailableContexts;

	u32 m_QueueIndex = UINT_MAX;
};

} // namespace vk