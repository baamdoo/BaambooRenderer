#include "RendererPch.h"
#include "VkTimer.h"
#include "VkCommandContext.h"

namespace vk
{

void VkTimer::Init(VkDevice vkDevice, u32 numQueries)
{
	m_NumQueries = numQueries;

	VkQueryPoolCreateInfo poolInfo = {
		.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType  = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = numQueries
	};
	VK_CHECK(vkCreateQueryPool(vkDevice, &poolInfo, nullptr, &m_QueryPool));

	m_bFirstQuery = true;
}

void VkTimer::Destroy(VkDevice vkDevice)
{
	vkDestroyQueryPool(vkDevice, m_QueryPool, nullptr);
}

void VkTimer::Start(VkCommandBuffer vkCmdBuffer)
{
	m_bFirstQuery  = false;
	m_QueryCounter = 0;

	vkCmdResetQueryPool(vkCmdBuffer, m_QueryPool, 0, m_NumQueries);
	vkCmdWriteTimestamp(vkCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_QueryPool, 0);
}

void VkTimer::End(VkCommandBuffer vkCmdBuffer)
{
	m_QueryCounter++;
	vkCmdWriteTimestamp(vkCmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool, m_QueryCounter);
}

double VkTimer::GetElapsedTime(VkDevice vkDevice, const VkPhysicalDeviceProperties& deviceProps) const
{
	if (m_bFirstQuery)
		return 0.0;

	std::vector< u64 > queryResults(m_NumQueries);
	VK_CHECK(vkGetQueryPoolResults(vkDevice, m_QueryPool, 0, static_cast<u32>(queryResults.size()), sizeof(queryResults), queryResults.data(), sizeof(queryResults[0]), VK_QUERY_RESULT_64_BIT));

	double gpuTimeElapsed =
		double(queryResults[1]) * deviceProps.limits.timestampPeriod
		- double(queryResults[0]) * deviceProps.limits.timestampPeriod;

	return gpuTimeElapsed;
}

} // namespace Baamboo