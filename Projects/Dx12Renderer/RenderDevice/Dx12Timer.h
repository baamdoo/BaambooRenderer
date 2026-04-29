#pragma once
#include "RenderCommon/CommandContext.h"

namespace dx12
{

using namespace Microsoft::WRL;

// =========================================================================
// Dx12Timer — per-frame multi-scope GPU timestamp + pipeline-stats profiler.
//
//   Requires ID3D12Device9 + D3D12_FEATURE_D3D12_OPTIONS7 MeshShaderTier >= 1.
//   Falls back to PIPELINE_STATISTICS (no MS/AS counters) otherwise.
// =========================================================================
class Dx12Timer
{
public:
	Dx12Timer() = default;
	~Dx12Timer() = default;

	void Init(ID3D12Device* d3d12Device, ID3D12CommandQueue* d3d12CommandQueue, UINT maxQueriesPerFrame = 128);
	void Destroy();

	// Frame lifecycle
	void BeginFrame(ID3D12GraphicsCommandList* d3d12CommandList);
	void EndFrame(ID3D12GraphicsCommandList* d3d12CommandList);

	// Scoped markers (called between BeginFrame and EndFrame)
	void BeginMarker(ID3D12GraphicsCommandList* d3d12CommandList, const char* name, bool bStats = false);
	void EndMarker(ID3D12GraphicsCommandList* d3d12CommandList);

	// Results from previous completed frame
	const std::vector< render::GpuProfileEntry >& GetLastFrameProfile() const { return m_LastResults; }
	double GetLastFrameTotalNs() const;

private:
	// Timestamps
	ComPtr< ID3D12QueryHeap > m_QueryHeap;
	ComPtr< ID3D12Resource >  m_ReadbackBuffer;

	// Pipeline statistics
	ComPtr< ID3D12QueryHeap > m_StatsHeap;
	ComPtr< ID3D12Resource >  m_StatsReadbackBuffer;
	D3D12_QUERY_TYPE          m_StatsQueryType     = D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
	UINT                      m_StatsBlobSize      = 0; // sizeof D3D12_QUERY_DATA_PIPELINE_STATISTICS[1]
	bool                      m_bStatsSupported    = false;
	bool                      m_bMeshShaderStats   = false;
	UINT                      m_MaxStatsQueries    = 0;
	UINT                      m_NextStatsIdx       = 0;

	UINT64 m_GpuFrequency  = 0;
	UINT   m_MaxQueries    = 0;
	UINT   m_NextQueryIdx  = 0;

	std::vector< render::GpuProfileEntry > m_Building;
	std::vector< render::GpuProfileEntry > m_LastResults;
	std::vector< std::pair< UINT, UINT > > m_QueryIndices;
	std::vector< UINT >                    m_StatsIndices; // parallel to m_Building: UINT32_MAX = no stats
	std::vector< UINT >                    m_OpenStack;
	UINT                                   m_CurrentDepth = 0;

	bool m_bHasPreviousFrame = false;
};

} // namespace dx12
