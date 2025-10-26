#include "RendererPch.h"
#include "Dx12CommandQueue.h"
#include "Dx12CommandContext.h"

namespace dx12
{

Dx12CommandQueue::Dx12CommandQueue(Dx12RenderDevice& rd, D3D12_COMMAND_LIST_TYPE type)
	: m_RenderDevice(rd)
	, m_Type(type)
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = m_Type;
	
	auto d3d12Device = m_RenderDevice.GetD3D12Device();
	ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_d3d12CommandQueue)));
	ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_d3d12Fence)));
}

Dx12CommandQueue::~Dx12CommandQueue()
{
	while (!m_pAvailableContexts.empty())
	{
		m_pAvailableContexts.pop();
	}
	m_pPendingContexts.clear();

	COM_RELEASE(m_d3d12Fence);
	COM_RELEASE(m_d3d12CommandQueue);
}

Arc< Dx12CommandContext > Dx12CommandQueue::Allocate()
{
	auto iter = m_pPendingContexts.begin();
	while (iter != m_pPendingContexts.end() && IsFenceComplete(iter->first))
	{
		m_pAvailableContexts.push(iter->second);
		iter = m_pPendingContexts.erase(iter);
	}

	Arc< Dx12CommandContext > pCommandList;
	if (!m_pAvailableContexts.empty())
	{
		pCommandList = m_pAvailableContexts.front();
		m_pAvailableContexts.pop();

		pCommandList->Open();
	}
	else
	{
		pCommandList = RequestList();
		pCommandList->Open();
	}

	return pCommandList;
}

Arc< Dx12CommandContext > Dx12CommandQueue::RequestList()
{
	return MakeArc< Dx12CommandContext >(m_RenderDevice, m_Type);
}

u64 Dx12CommandQueue::ExecuteCommandList(Arc< Dx12CommandContext > pContext)
{
	return ExecuteCommandLists({ pContext });
}

u64 Dx12CommandQueue::ExecuteCommandLists(const std::vector< Arc< Dx12CommandContext > >& pCommandContexts)
{
	std::vector< ID3D12CommandList* > d3d12CommandLists;
	d3d12CommandLists.reserve(pCommandContexts.size());

	for (auto pCommandList : pCommandContexts)
		d3d12CommandLists.push_back(pCommandList->GetD3D12CommandList());

	m_d3d12CommandQueue->ExecuteCommandLists(static_cast<u32>(d3d12CommandLists.size()), d3d12CommandLists.data());

	auto fenceValue = Signal();
	for (auto pContext : pCommandContexts)
		m_pPendingContexts.insert({ fenceValue, pContext });

	return fenceValue;
}

u64 Dx12CommandQueue::Signal()
{
	u64 fenceValue = ++m_FenceValue;
	ThrowIfFailed(m_d3d12CommandQueue->Signal(m_d3d12Fence, fenceValue));
	return fenceValue;
}

bool Dx12CommandQueue::IsFenceComplete(u64 fenceValue)
{
	if (fenceValue <= m_FenceCompletedValue)
		return true;

	m_FenceCompletedValue = m_d3d12Fence->GetCompletedValue();
	return fenceValue <= m_FenceCompletedValue;
}

void Dx12CommandQueue::WaitForFenceValue(u64 fenceValue)
{
	if (!IsFenceComplete(fenceValue))
	{
		auto event = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		if (event)
		{
			m_d3d12Fence->SetEventOnCompletion(fenceValue, event);
			::WaitForSingleObject(event, INFINITE);

			::CloseHandle(event);
		}
	}
}

void Dx12CommandQueue::Flush()
{
	WaitForFenceValue(Signal());

	auto iter = m_pPendingContexts.begin();
	while (iter != m_pPendingContexts.end())
	{
		m_pAvailableContexts.push(iter->second);
		iter = m_pPendingContexts.erase(iter);
	}
}

}