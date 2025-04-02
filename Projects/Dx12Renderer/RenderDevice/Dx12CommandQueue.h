#pragma once

namespace dx12
{

class CommandList;

class CommandQueue
{
protected:
	// Only the render context can instantiate command queue
	friend class RenderContext;

	CommandQueue(RenderContext& context, D3D12_COMMAND_LIST_TYPE type);
	~CommandQueue();

	CommandList& Allocate();
	CommandList* RequestList();

public:
	u64 ExecuteCommandList(CommandList* pCommandList);
	u64 ExecuteCommandLists(const std::vector< CommandList* >& pCommandLists);

	u64 Signal();
	bool IsFenceComplete(u64 fenceValue);
	void WaitForFenceValue(u64 fenceValue);
	void Flush();

public:
	inline ID3D12CommandQueue* GetD3D12CommandQueue() const { return m_d3d12CommandQueue; }

private:
	RenderContext& m_RenderContext;
	D3D12_COMMAND_LIST_TYPE m_Type;

	ID3D12CommandQueue*                m_d3d12CommandQueue = nullptr;
	std::queue< CommandList* >         m_AvailableLists;
	std::multimap< u64, CommandList* > m_PendingLists;

	ID3D12Fence* m_d3d12Fence = nullptr;
	u64 m_FenceValue = 0;
	u64 m_FenceCompletedValue = 0;
};

}