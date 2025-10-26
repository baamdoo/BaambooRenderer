#pragma once

namespace dx12
{

class CommandContext;

class Dx12CommandQueue
{
public:
	Dx12CommandQueue(Dx12RenderDevice& rd, D3D12_COMMAND_LIST_TYPE type);
	~Dx12CommandQueue();

	Arc< Dx12CommandContext > Allocate();
	Arc< Dx12CommandContext > RequestList();

	u64 ExecuteCommandList(Arc< Dx12CommandContext > pCommandContext);
	u64 ExecuteCommandLists(const std::vector< Arc< Dx12CommandContext > >& pCommandContexts);

	u64 Signal();
	bool IsFenceComplete(u64 fenceValue);
	void WaitForFenceValue(u64 fenceValue);
	void Flush();

	[[nodiscard]]
	inline ID3D12CommandQueue* GetD3D12CommandQueue() const { return m_d3d12CommandQueue; }

private:
	Dx12RenderDevice& m_RenderDevice;
	D3D12_COMMAND_LIST_TYPE m_Type;

	ID3D12CommandQueue* m_d3d12CommandQueue = nullptr;

	std::queue< Arc< Dx12CommandContext > >         m_pAvailableContexts;
	std::multimap< u64, Arc< Dx12CommandContext > > m_pPendingContexts;

	ID3D12Fence* m_d3d12Fence = nullptr;
	u64 m_FenceValue = 0;
	u64 m_FenceCompletedValue = 0;
};

}