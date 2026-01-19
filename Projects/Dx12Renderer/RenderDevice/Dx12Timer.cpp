#include "RendererPch.h"
#include "Dx12Timer.h"

namespace dx12
{

void Dx12Timer::Init(ID3D12Device* d3d12Device, ID3D12CommandQueue* d3d12CommandQueue, UINT numQueries)
{
	m_NumQueries  = numQueries;
	m_bFirstQuery = true;

	D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
	queryHeapDesc.Count    = numQueries;
	queryHeapDesc.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queryHeapDesc.NodeMask = 0;
	DX_CHECK(d3d12Device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_QueryHeap)));

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type                 = D3D12_HEAP_TYPE_READBACK;
	heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask     = 1;
	heapProps.VisibleNodeMask      = 1;

	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Alignment          = 0;
	bufferDesc.Width              = static_cast<UINT64>(numQueries) * sizeof(UINT64);
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
		IID_PPV_ARGS(&m_ReadbackBuffer)
	));

	DX_CHECK(d3d12CommandQueue->GetTimestampFrequency(&m_GpuFrequency));
}

void Dx12Timer::Destroy()
{
	m_QueryHeap.Reset();
	m_ReadbackBuffer.Reset();
}

void Dx12Timer::Start(ID3D12GraphicsCommandList* d3d12CommandList)
{
	m_bFirstQuery  = false;
	m_QueryCounter = 0;

	d3d12CommandList->EndQuery(m_QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
}

void Dx12Timer::End(ID3D12GraphicsCommandList* d3d12CommandList)
{
	m_QueryCounter++;

	d3d12CommandList->EndQuery(m_QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
	d3d12CommandList->ResolveQueryData(
		m_QueryHeap.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP,
		0,
		m_NumQueries,
		m_ReadbackBuffer.Get(),
		0
	);
}

double Dx12Timer::GetElapsedTime() const
{
	if (m_bFirstQuery || m_GpuFrequency == 0)
		return 0.0;

	UINT64* pData = nullptr;
	D3D12_RANGE readRange = { 0, m_NumQueries * sizeof(UINT64) };

	HRESULT hr = m_ReadbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));

	double elapsedNs = 0.0;
	if (SUCCEEDED(hr) && pData != nullptr)
	{
		UINT64 startTime = pData[0];
		UINT64 endTime   = pData[1];

		UINT64 delta = endTime - startTime;

		elapsedNs = (static_cast<double>(delta) / static_cast<double>(m_GpuFrequency)) * 1e12;
		elapsedNs = (double(delta) / double(m_GpuFrequency)) * 1e9;

		D3D12_RANGE writeRange = { 0, 0 };
		m_ReadbackBuffer->Unmap(0, &writeRange);
	}

	return elapsedNs;
}

} // namespace dx12