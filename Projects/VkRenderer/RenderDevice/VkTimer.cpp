#include "RendererPch.h"
#include "VkTimer.h"
#include "VkCommandContext.h"

namespace vk
{

static PFN_vkCmdBeginDebugUtilsLabelEXT s_pfnCmdBeginDebugUtilsLabelEXT = nullptr;
static PFN_vkCmdEndDebugUtilsLabelEXT   s_pfnCmdEndDebugUtilsLabelEXT   = nullptr;
static bool                             s_bDebugLabelLoaded             = false;

static void LoadDebugUtilsFunctions(VkDevice vkDevice)
{
	if (s_bDebugLabelLoaded)
		return;
	s_pfnCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(vkDevice, "vkCmdBeginDebugUtilsLabelEXT");
	s_pfnCmdEndDebugUtilsLabelEXT   = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(vkDevice, "vkCmdEndDebugUtilsLabelEXT");
	s_bDebugLabelLoaded             = true;
}

static void UnpackColor(u32 packed, float out[4])
{
	out[0] = float((packed >>  0) & 0xFF) / 255.0f;
	out[1] = float((packed >>  8) & 0xFF) / 255.0f;
	out[2] = float((packed >> 16) & 0xFF) / 255.0f;
	out[3] = float((packed >> 24) & 0xFF) / 255.0f;
}

void VkTimer::Init(VkDevice vkDevice, const VkPhysicalDeviceFeatures& deviceFeatures, u32 maxQueriesPerFrame)
{
	m_MaxQueries      = maxQueriesPerFrame;
	m_MaxStatsQueries = maxQueriesPerFrame / 2; // stats queries are per-scope, not per-begin+end

	VkQueryPoolCreateInfo poolInfo = {
		.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType  = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = m_MaxQueries,
	};
	VK_CHECK(vkCreateQueryPool(vkDevice, &poolInfo, nullptr, &m_QueryPool));

	// Pipeline statistics pool
	m_bStatsSupported  = deviceFeatures.pipelineStatisticsQuery == VK_TRUE;

	if (m_bStatsSupported)
	{
		// Order matters — results come back in this bit order.
		m_StatsFlags =
			VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
			VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
			VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT      |
			VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT       |
			VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
			VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;

		m_NumStatFields = 6;

		VkQueryPoolCreateInfo statsPoolInfo = {
			.sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
			.queryType          = VK_QUERY_TYPE_PIPELINE_STATISTICS,
			.queryCount         = m_MaxStatsQueries,
			.pipelineStatistics = m_StatsFlags,
		};
		VK_CHECK(vkCreateQueryPool(vkDevice, &statsPoolInfo, nullptr, &m_StatsPool));
	}

	LoadDebugUtilsFunctions(vkDevice);

	m_Building.reserve(64);
	m_LastResults.reserve(64);
	m_QueryIndices.reserve(64);
	m_StatsIndices.reserve(64);
	m_OpenStack.reserve(16);
}

void VkTimer::Destroy(VkDevice vkDevice)
{
	if (m_QueryPool != VK_NULL_HANDLE)
	{
		vkDestroyQueryPool(vkDevice, m_QueryPool, nullptr);
		m_QueryPool = VK_NULL_HANDLE;
	}
	if (m_StatsPool != VK_NULL_HANDLE)
	{
		vkDestroyQueryPool(vkDevice, m_StatsPool, nullptr);
		m_StatsPool = VK_NULL_HANDLE;
	}
}

void VkTimer::BeginFrame(VkCommandBuffer vkCmdBuffer, VkDevice vkDevice, const VkPhysicalDeviceProperties& deviceProps)
{
	// 1) Read previous frame's results from the readback buffers.
	if (m_bHasPreviousFrame && !m_Building.empty())
	{
		// --- Timestamps ---
		const u32 numQueriesWritten = m_NextQueryIdx;
		std::vector< u64 > ticks(numQueriesWritten);
		vkGetQueryPoolResults(
			vkDevice,
			m_QueryPool,
			0, numQueriesWritten,
			ticks.size() * sizeof(u64),
			ticks.data(),
			sizeof(u64),
			VK_QUERY_RESULT_64_BIT);

		const double tickToMs = double(deviceProps.limits.timestampPeriod) * 1e-6;
		for (size_t i = 0; i < m_Building.size(); ++i)
		{
			const auto [startIdx, endIdx] = m_QueryIndices[i];
			const u64  delta = ticks[endIdx] - ticks[startIdx];
			m_Building[i].elapsedMs       = double(delta) * tickToMs;
		}

		// --- Pipeline statistics ---
		if (m_bStatsSupported && m_NextStatsIdx > 0)
		{
			std::vector< u64 > stats(m_NextStatsIdx * m_NumStatFields);
			vkGetQueryPoolResults(
				vkDevice,
				m_StatsPool,
				0, m_NextStatsIdx,
				stats.size() * sizeof(u64),
				stats.data(),
				m_NumStatFields * sizeof(u64),
				VK_QUERY_RESULT_64_BIT);

			for (size_t i = 0; i < m_Building.size(); ++i)
			{
				const u32 sidx = m_StatsIndices[i];
				if (sidx == UINT32_MAX)
					continue;

				const u64* p = stats.data() + sidx * m_NumStatFields;
				render::GpuPipelineStats s = {};
				s.iaPrimitives        = p[0];
				s.vsInvocations       = p[1];
				s.clippingInvocations = p[2];
				s.clippingPrimitives  = p[3];
				s.fsInvocations       = p[4];
				s.csInvocations       = p[5];

				m_Building[i].stats     = s;
				m_Building[i].bHasStats = true;
			}
		}

		m_LastResults = std::move(m_Building);
	}

	// 2) Reset bookkeeping for new frame
	m_Building.clear();
	m_QueryIndices.clear();
	m_StatsIndices.clear();
	m_OpenStack.clear();
	m_NextQueryIdx = 0;
	m_NextStatsIdx = 0;
	m_CurrentDepth = 0;

	// 3) Reset GPU pools and open the implicit "Frame" scope
	vkCmdResetQueryPool(vkCmdBuffer, m_QueryPool, 0, m_MaxQueries);
	if (m_bStatsSupported)
		vkCmdResetQueryPool(vkCmdBuffer, m_StatsPool, 0, m_MaxStatsQueries);

	BeginMarker(vkCmdBuffer, "Frame");

	m_bHasPreviousFrame = true;
}

void VkTimer::EndFrame(VkCommandBuffer vkCmdBuffer)
{
	EndMarker(vkCmdBuffer); // close implicit "Frame"
	assert(m_OpenStack.empty() && "Unbalanced BeginGpuMarker / EndGpuMarker");
}

void VkTimer::BeginMarker(VkCommandBuffer vkCmdBuffer, const char* name, bool bStats)
{
	if (m_NextQueryIdx + 2 > m_MaxQueries)
	{
		assert(false && "GPU profiler query pool overflow — increase maxQueriesPerFrame");
		return;
	}

	const u32 startIdx = m_NextQueryIdx++;

	render::GpuProfileEntry entry = {
		.name      = name,
		.depth     = m_CurrentDepth,
		.elapsedMs = 0.0,
	};

	const u32 entryIdx = u32(m_Building.size());
	m_Building.push_back(entry);
	m_QueryIndices.emplace_back(startIdx, ~0u);

	// Stats query
	u32 statsIdx = UINT32_MAX;
	if (bStats && m_bStatsSupported && m_NextStatsIdx < m_MaxStatsQueries)
	{
		statsIdx = m_NextStatsIdx++;
		vkCmdBeginQuery(vkCmdBuffer, m_StatsPool, statsIdx, 0);
	}
	m_StatsIndices.push_back(statsIdx);

	m_OpenStack.push_back(entryIdx);

	// RenderDoc label so the range visually encompasses the timestamp.
	if (s_pfnCmdBeginDebugUtilsLabelEXT)
	{
		VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
		label.pLabelName = name;
		UnpackColor(render::GetGpuMarkerColor(name), label.color);
		s_pfnCmdBeginDebugUtilsLabelEXT(vkCmdBuffer, &label);
	}

	vkCmdWriteTimestamp(vkCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_QueryPool, startIdx);

	++m_CurrentDepth;
}

void VkTimer::EndMarker(VkCommandBuffer vkCmdBuffer)
{
	assert(!m_OpenStack.empty() && "EndGpuMarker without matching BeginGpuMarker");
	if (m_OpenStack.empty())
		return;

	const u32 entryIdx = m_OpenStack.back();
	m_OpenStack.pop_back();

	const u32 endIdx = m_NextQueryIdx++;
	m_QueryIndices[entryIdx].second = endIdx;

	vkCmdWriteTimestamp(vkCmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool, endIdx);

	// Close stats query if this scope had one.
	const u32 statsIdx = m_StatsIndices[entryIdx];
	if (statsIdx != UINT32_MAX)
		vkCmdEndQuery(vkCmdBuffer, m_StatsPool, statsIdx);

	// End label after the timestamp so the range fully encloses it.
	if (s_pfnCmdEndDebugUtilsLabelEXT)
		s_pfnCmdEndDebugUtilsLabelEXT(vkCmdBuffer);

	--m_CurrentDepth;
}

double VkTimer::GetLastFrameTotalNs() const
{
	if (m_LastResults.empty())
		return 0.0;
	return m_LastResults[0].elapsedMs * 1e6; // ms → ns for backward-compat
}

} // namespace vk
