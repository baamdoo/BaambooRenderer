#include "RendererPch.h"
#include "Dx12Timer.h"

#ifndef USE_PIX
#define USE_PIX
#endif
#include <pix3.h>

namespace dx12
{

void Dx12Timer::Init(ID3D12Device* d3d12Device, ID3D12CommandQueue* d3d12CommandQueue, UINT maxQueriesPerFrame)
{
	m_MaxQueries      = maxQueriesPerFrame;
	m_MaxStatsQueries = maxQueriesPerFrame / 2; // one stats query per scope, not per begin+end

	// --- Timestamp heap + readback buffer ---
	D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
	queryHeapDesc.Count    = m_MaxQueries;
	queryHeapDesc.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queryHeapDesc.NodeMask = 0;
	DX_CHECK(d3d12Device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_QueryHeap)));

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type                 = D3D12_HEAP_TYPE_READBACK;
	heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask     = 1;
	heapProps.VisibleNodeMask      = 1;

	auto makeReadbackBuffer = [&](UINT64 width, ID3D12Resource** out)
	{
		D3D12_RESOURCE_DESC bufferDesc = {};
		bufferDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Alignment          = 0;
		bufferDesc.Width              = width;
		bufferDesc.Height             = 1;
		bufferDesc.DepthOrArraySize   = 1;
		bufferDesc.MipLevels          = 1;
		bufferDesc.Format             = DXGI_FORMAT_UNKNOWN;
		bufferDesc.SampleDesc.Count   = 1;
		bufferDesc.SampleDesc.Quality = 0;
		bufferDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufferDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

		DX_CHECK(d3d12Device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(out)
		));
	};

	makeReadbackBuffer(static_cast<UINT64>(m_MaxQueries) * sizeof(UINT64), &m_ReadbackBuffer);

	DX_CHECK(d3d12CommandQueue->GetTimestampFrequency(&m_GpuFrequency));

	// --- Pipeline statistics heap + readback buffer ---
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7 = {};
	if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opts7, sizeof(opts7)))
	    && opts7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1)
	{
		m_bMeshShaderStats = true;
		m_StatsQueryType   = D3D12_QUERY_TYPE_PIPELINE_STATISTICS1;
		m_StatsBlobSize    = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1);
	}
	else
	{
		m_StatsQueryType = D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
		m_StatsBlobSize  = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
	}

	D3D12_QUERY_HEAP_DESC statsHeapDesc = {};
	statsHeapDesc.Count    = m_MaxStatsQueries;
	statsHeapDesc.Type     = m_bMeshShaderStats ? D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1
	                                            : D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
	statsHeapDesc.NodeMask = 0;
	if (SUCCEEDED(d3d12Device->CreateQueryHeap(&statsHeapDesc, IID_PPV_ARGS(&m_StatsHeap))))
	{
		makeReadbackBuffer(static_cast<UINT64>(m_MaxStatsQueries) * m_StatsBlobSize, &m_StatsReadbackBuffer);
		m_bStatsSupported = true;
	}

	m_Building.reserve(64);
	m_LastResults.reserve(64);
	m_QueryIndices.reserve(64);
	m_StatsIndices.reserve(64);
	m_OpenStack.reserve(16);
}

void Dx12Timer::Destroy()
{
	m_QueryHeap.Reset();
	m_ReadbackBuffer.Reset();
	m_StatsHeap.Reset();
	m_StatsReadbackBuffer.Reset();
}

void Dx12Timer::BeginFrame(ID3D12GraphicsCommandList* d3d12CommandList)
{
	// 1) Read previous frame's results from the readback buffers.
	if (m_bHasPreviousFrame && !m_Building.empty() && m_GpuFrequency != 0)
	{
		const UINT  numQueriesWritten = m_NextQueryIdx;
		D3D12_RANGE readRange         = { 0, numQueriesWritten * sizeof(UINT64) };
		UINT64*     pTicks            = nullptr;

		HRESULT hr = m_ReadbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pTicks));
		if (SUCCEEDED(hr) && pTicks != nullptr)
		{
			const double tickToMs = (1.0 / static_cast<double>(m_GpuFrequency)) * 1e3;
			for (size_t i = 0; i < m_Building.size(); ++i)
			{
				const auto [startIdx, endIdx] = m_QueryIndices[i];
				const UINT64 delta = pTicks[endIdx] - pTicks[startIdx];
				m_Building[i].elapsedMs = static_cast<double>(delta) * tickToMs;
			}

			D3D12_RANGE writeRange = { 0, 0 };
			m_ReadbackBuffer->Unmap(0, &writeRange);
		}

		// Pipeline statistics
		if (m_bStatsSupported && m_NextStatsIdx > 0 && m_StatsReadbackBuffer)
		{
			D3D12_RANGE statsRange = { 0, m_NextStatsIdx * m_StatsBlobSize };
			BYTE*       pStats     = nullptr;
			if (SUCCEEDED(m_StatsReadbackBuffer->Map(0, &statsRange, reinterpret_cast<void**>(&pStats))) && pStats)
			{
				for (size_t i = 0; i < m_Building.size(); ++i)
				{
					const UINT sidx = m_StatsIndices[i];
					if (sidx == UINT32_MAX)
						continue;

					const BYTE* p = pStats + sidx * m_StatsBlobSize;
					render::GpuPipelineStats s = {};

					if (m_bMeshShaderStats)
					{
						const auto* d = reinterpret_cast<const D3D12_QUERY_DATA_PIPELINE_STATISTICS1*>(p);
						s.iaPrimitives        = d->IAPrimitives;
						s.vsInvocations       = d->VSInvocations;
						s.clippingInvocations = d->CInvocations;
						s.clippingPrimitives  = d->CPrimitives;
						s.fsInvocations       = d->PSInvocations;
						s.csInvocations       = d->CSInvocations;
						s.taskInvocations     = d->ASInvocations;
						s.meshInvocations     = d->MSInvocations;
						s.bHasMeshCounters    = true;
					}
					else
					{
						const auto* d = reinterpret_cast<const D3D12_QUERY_DATA_PIPELINE_STATISTICS*>(p);
						s.iaPrimitives        = d->IAPrimitives;
						s.vsInvocations       = d->VSInvocations;
						s.clippingInvocations = d->CInvocations;
						s.clippingPrimitives  = d->CPrimitives;
						s.fsInvocations       = d->PSInvocations;
						s.csInvocations       = d->CSInvocations;
					}

					m_Building[i].stats     = s;
					m_Building[i].bHasStats = true;
				}

				D3D12_RANGE writeRange = { 0, 0 };
				m_StatsReadbackBuffer->Unmap(0, &writeRange);
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

	// 3) Open the implicit "Frame" scope (DX12 has no explicit query heap reset)
	BeginMarker(d3d12CommandList, "Frame");

	m_bHasPreviousFrame = true;
}

void Dx12Timer::EndFrame(ID3D12GraphicsCommandList* d3d12CommandList)
{
	EndMarker(d3d12CommandList); // close implicit "Frame"
	assert(m_OpenStack.empty() && "Unbalanced BeginGpuMarker / EndGpuMarker");

	// Resolve the entire range of timestamps used this frame to the readback buffer.
	if (m_NextQueryIdx > 0)
	{
		d3d12CommandList->ResolveQueryData(
			m_QueryHeap.Get(),
			D3D12_QUERY_TYPE_TIMESTAMP,
			0,
			m_NextQueryIdx,
			m_ReadbackBuffer.Get(),
			0
		);
	}

	// Resolve pipeline statistics
	if (m_bStatsSupported && m_NextStatsIdx > 0 && m_StatsHeap && m_StatsReadbackBuffer)
	{
		d3d12CommandList->ResolveQueryData(
			m_StatsHeap.Get(),
			m_StatsQueryType,
			0,
			m_NextStatsIdx,
			m_StatsReadbackBuffer.Get(),
			0
		);
	}
}

void Dx12Timer::BeginMarker(ID3D12GraphicsCommandList* d3d12CommandList, const char* name, bool bStats)
{
	if (m_NextQueryIdx + 2 > m_MaxQueries)
	{
		assert(false && "GPU profiler query heap overflow — increase maxQueriesPerFrame");
		return;
	}

	const UINT startIdx = m_NextQueryIdx++;

	render::GpuProfileEntry entry = {
		.name      = name,
		.depth     = m_CurrentDepth,
		.elapsedMs = 0.0,
	};

	const UINT entryIdx = static_cast<UINT>(m_Building.size());
	m_Building.push_back(entry);
	m_QueryIndices.emplace_back(startIdx, ~0u);

	// Stats query
	UINT statsIdx = UINT32_MAX;
	if (bStats && m_bStatsSupported && m_NextStatsIdx < m_MaxStatsQueries)
	{
		statsIdx = m_NextStatsIdx++;
		d3d12CommandList->BeginQuery(m_StatsHeap.Get(), m_StatsQueryType, statsIdx);
	}
	m_StatsIndices.push_back(statsIdx);

	m_OpenStack.push_back(entryIdx);

	// PIX/RenderDoc label so the range visually encompasses the timestamp.
	if (name && name[0])
	{
		const u32 packed = render::GetGpuMarkerColor(name);

		const BYTE r = static_cast<BYTE>((packed >>  0) & 0xFF);
		const BYTE g = static_cast<BYTE>((packed >>  8) & 0xFF);
		const BYTE b = static_cast<BYTE>((packed >> 16) & 0xFF);
		PIXBeginEvent(d3d12CommandList, PIX_COLOR(r, g, b), name);
	}

	d3d12CommandList->EndQuery(m_QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startIdx);

	++m_CurrentDepth;
}

void Dx12Timer::EndMarker(ID3D12GraphicsCommandList* d3d12CommandList)
{
	assert(!m_OpenStack.empty() && "EndGpuMarker without matching BeginGpuMarker");
	if (m_OpenStack.empty())
		return;

	const UINT entryIdx = m_OpenStack.back();
	m_OpenStack.pop_back();

	const UINT endIdx = m_NextQueryIdx++;
	m_QueryIndices[entryIdx].second = endIdx;

	d3d12CommandList->EndQuery(m_QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, endIdx);

	// Close stats query if this scope had one.
	const UINT statsIdx = m_StatsIndices[entryIdx];
	if (statsIdx != UINT32_MAX)
		d3d12CommandList->EndQuery(m_StatsHeap.Get(), m_StatsQueryType, statsIdx);

	// End label after the timestamp so the range fully encloses it.
	PIXEndEvent(d3d12CommandList);

	--m_CurrentDepth;
}

double Dx12Timer::GetLastFrameTotalNs() const
{
	if (m_LastResults.empty())
		return 0.0;
	return m_LastResults[0].elapsedMs * 1e6; // ms → ns for backward-compat
}

} // namespace dx12
