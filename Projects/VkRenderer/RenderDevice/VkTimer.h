#pragma once
#include "RenderCommon/RendererAPI.h"
#include "RenderCommon/CommandContext.h"

namespace vk
{

// =============================================================================
// VkTimer — per-frame multi-scope GPU timestamp + pipeline-stats profiler.
//
//   Collected only when the device supports pipelineStatisticsQuery. 
//   Mesh/task invocations require VK_EXT_mesh_shader meshShaderQueries feature.
// =============================================================================
class VkTimer
{
public:
	void Init(
		VkDevice vkDevice,
		bool bPipelineStatisticsEnabled,
		u32 timestampValidBits,
		PFN_vkCmdBeginDebugUtilsLabelEXT cmdBeginDebugUtilsLabel,
		PFN_vkCmdEndDebugUtilsLabelEXT cmdEndDebugUtilsLabel,
		u32 maxQueriesPerFrame = 128);
	void Destroy(VkDevice vkDevice);

	// Frame lifecycle
	void BeginFrame(VkCommandBuffer vkCmdBuffer, VkDevice vkDevice, const VkPhysicalDeviceProperties& deviceProps);
	void EndFrame(VkCommandBuffer vkCmdBuffer);

	// Scoped markers (called between BeginFrame and EndFrame)
	void BeginMarker(VkCommandBuffer vkCmdBuffer, const char* name, bool bStats = false);
	void EndMarker(VkCommandBuffer vkCmdBuffer);

	// Results from previous completed frame
	const std::vector< render::GpuProfileEntry >& GetLastFrameProfile() const { return m_LastResults; }
	double GetLastFrameTotalNs() const;

private:
	// Timestamp pool
	VkQueryPool m_QueryPool    = VK_NULL_HANDLE;
	u32         m_MaxQueries   = 0;
	u32         m_NextQueryIdx = 0;
	u32         m_TimestampValidBits = 0;
	bool        m_bEnabled = false;

	VkQueryPool m_StatsPool       = VK_NULL_HANDLE;
	u32         m_MaxStatsQueries = 0;
	u32         m_NextStatsIdx    = 0;
	u32         m_NumStatFields   = 0;
	bool        m_bStatsSupported = false;

	PFN_vkCmdBeginDebugUtilsLabelEXT m_CmdBeginDebugUtilsLabel = nullptr;
	PFN_vkCmdEndDebugUtilsLabelEXT   m_CmdEndDebugUtilsLabel = nullptr;

	VkQueryPipelineStatisticFlags m_StatsFlags = 0;

	std::vector< render::GpuProfileEntry > m_Building;     // current frame entries (filled as markers are emitted)
	std::vector< render::GpuProfileEntry > m_LastResults;  // previous frame entries with elapsedMs filled
	std::vector< std::pair< u32, u32 > >   m_QueryIndices; // parallel to m_Building: (startIdx, endIdx)
	std::vector< u32 >                     m_StatsIndices; // parallel to m_Building: UINT32_MAX if no stats query
	std::vector< u32 >                     m_OpenStack;    // indices into m_Building of currently-open scopes
	u32                                    m_CurrentDepth = 0;

	bool m_bHasPreviousFrame = false;
};

} // namespace vk
