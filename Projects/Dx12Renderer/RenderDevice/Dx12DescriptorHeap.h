#pragma once
#include "Dx12RootSignature.h"
#include "Dx12DescriptorAllocation.h"

namespace dx12
{
	
class DescriptorPool;

class DescriptorHeap
{
public:
	DescriptorHeap(RenderDevice& device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 maxDescriptors = 1024);
	~DescriptorHeap();

	void Reset();
	void ParseRootSignature(RootSignature* pRootsignature);

	u32 StageDescriptors(u32 rootIndex, u32 numDescriptors, u32 offset, D3D12_CPU_DESCRIPTOR_HANDLE srcHandle);
	u32 StageDescriptors(u32 rootIndex, u32 offset, std::vector< D3D12_CPU_DESCRIPTOR_HANDLE >&& srcHandles);

	void CommitDescriptorsForDraw(CommandContext& commandList);
	void CommitDescriptorsForDispatch(CommandContext& commandList);

private:
	void CommitDescriptorTables(
		CommandContext& commandList,
		std::function< void(ID3D12GraphicsCommandList*, u32, D3D12_GPU_DESCRIPTOR_HANDLE) > setFunc);

public:
	ID3D12DescriptorHeap* GetD3D12DescriptorHeap() const;

private:
	RenderDevice& m_RenderDevice;

	u32 m_NumDescriptors = 0;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;

	DescriptorPool* m_pDescriptorPool = nullptr;

	u64 m_DescriptorTableBitMask    = 0;
	u64 m_DescriptorTableDirtyFlags = 0;
	std::unordered_map< RootSignature*, std::array< DescriptorAllocation, MAX_ROOT_INDEX > > m_CachedDescriptorAllocations = {};

	RootSignature* m_pCurrentRS = nullptr;
};

}