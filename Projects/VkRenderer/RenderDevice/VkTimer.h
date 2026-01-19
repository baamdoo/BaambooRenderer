#pragma once
#include "RenderCommon/RendererAPI.h"

namespace vk
{

class VkTimer
{
public:
	void Init(VkDevice vkDevice, u32 numQueries);
	void Destroy(VkDevice vkDevice);

	void Start(VkCommandBuffer vkCmdBuffer);
	void End(VkCommandBuffer vkCmdBuffer);

	double GetElapsedTime(VkDevice vkDevice, const VkPhysicalDeviceProperties& deviceProps) const;

private:
	VkQueryPool m_QueryPool    = VK_NULL_HANDLE;

	u32 m_NumQueries   = 0;
	u32 m_QueryCounter = 0;

	bool m_bFirstQuery = false;
};

} // namespace Baamboo
