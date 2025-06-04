#pragma once

namespace dx12
{

class CommandContext;

class CommandQueue
{
public:
	CommandQueue(RenderDevice& device, D3D12_COMMAND_LIST_TYPE type);
	~CommandQueue();

	CommandContext* RequestList();
	CommandContext& Allocate();

	u64 ExecuteCommandList(CommandContext* pCommandContext);
	u64 ExecuteCommandLists(const std::vector< CommandContext* >& pCommandContexts);

	u64 Signal();
	bool IsFenceComplete(u64 fenceValue);
	void WaitForFenceValue(u64 fenceValue);
	void Flush();

	[[nodiscard]]
	inline ID3D12CommandQueue* GetD3D12CommandQueue() const { return m_d3d12CommandQueue; }

private:
	RenderDevice& m_RenderDevice;
	D3D12_COMMAND_LIST_TYPE m_Type;

	ID3D12CommandQueue*                   m_d3d12CommandQueue = nullptr;
	std::queue< CommandContext* >         m_pAvailableContexts;
	std::multimap< u64, CommandContext* > m_pPendingContexts;

	ID3D12Fence* m_d3d12Fence = nullptr;
	u64 m_FenceValue = 0;
	u64 m_FenceCompletedValue = 0;
};

}