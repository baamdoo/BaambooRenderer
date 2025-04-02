#include "RendererPch.h"
#include "Dx12CommandQueue.h"
#include "Dx12CommandList.h"

namespace dx12
{

CommandQueue::CommandQueue(RenderContext& context, D3D12_COMMAND_LIST_TYPE type)
	: m_RenderContext(context)
	, m_Type(type)
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = m_Type;
	
	auto d3d12Device = m_RenderContext.GetD3D12Device();
	ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_d3d12CommandQueue)));
	ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_d3d12Fence)));
}

CommandQueue::~CommandQueue()
{
	while (!m_AvailableLists.empty())
	{
		auto cmdList = m_AvailableLists.front();
		m_AvailableLists.pop();

		RELEASE(cmdList);
	}

	COM_RELEASE(m_d3d12Fence);
	COM_RELEASE(m_d3d12CommandQueue);
}

CommandList& CommandQueue::Allocate()
{
	auto iter = m_PendingLists.begin();
	while (iter != m_PendingLists.end() && IsFenceComplete(iter->first))
	{
		m_AvailableLists.push(iter->second);
		iter = m_PendingLists.erase(iter);
	}

	CommandList* pCommandList = nullptr;
	if (!m_AvailableLists.empty())
	{
		pCommandList = m_AvailableLists.front();
		m_AvailableLists.pop();

		pCommandList->Open();
	}
	else
	{
		pCommandList = RequestList();

		pCommandList->Open();
	}

	return *pCommandList;
}

CommandList* CommandQueue::RequestList()
{
	auto pCommandList = new CommandList(m_RenderContext, m_Type);
	return pCommandList;
}

u64 CommandQueue::ExecuteCommandList(CommandList* pCommandList)
{
	return ExecuteCommandLists({ pCommandList });
}

u64 CommandQueue::ExecuteCommandLists(const std::vector< CommandList* >& pCommandLists)
{
	std::vector< ID3D12CommandList* > d3d12CommandLists;
	d3d12CommandLists.reserve(pCommandLists.size());

	for (auto pCommandList : pCommandLists)
		d3d12CommandLists.push_back(pCommandList->GetD3D12CommandList());

	m_d3d12CommandQueue->ExecuteCommandLists(static_cast<u32>(d3d12CommandLists.size()), d3d12CommandLists.data());

	auto fenceValue = Signal();
	for (auto pCommandList : pCommandLists)
		m_PendingLists.insert({ fenceValue, pCommandList });

	return fenceValue;
}

u64 CommandQueue::Signal()
{
	u64 fenceValue = ++m_FenceValue;
	ThrowIfFailed(m_d3d12CommandQueue->Signal(m_d3d12Fence, fenceValue));
	return fenceValue;
}

bool CommandQueue::IsFenceComplete(u64 fenceValue)
{
	if (fenceValue <= m_FenceCompletedValue)
		return true;

	m_FenceCompletedValue = m_d3d12Fence->GetCompletedValue();
	return fenceValue <= m_FenceCompletedValue;
}

void CommandQueue::WaitForFenceValue(u64 fenceValue)
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

void CommandQueue::Flush()
{
	WaitForFenceValue(Signal());

	auto iter = m_PendingLists.begin();
	while (iter != m_PendingLists.end())
	{
		m_AvailableLists.push(iter->second);
		iter = m_PendingLists.erase(iter);
	}
}

}