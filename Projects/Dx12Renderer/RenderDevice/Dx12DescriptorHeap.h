#pragma once
#include "Dx12RootSignature.h"
#include "Dx12DescriptorAllocation.h"

namespace dx12
{
	
class DescriptorPool;

class DescriptorHeap
{
public:
	DescriptorHeap(RenderContext& context, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 maxDescriptors = 1024);
	~DescriptorHeap();

	void Reset();
	void ParseRootSignature(const RootSignature* pRootsignature);

	void StageDescriptors(u32 rootIndex, u32 numDescriptors, u32 offset, D3D12_CPU_DESCRIPTOR_HANDLE srcHandle);

	void CommitDescriptorsForDraw(CommandList& commandList);
	void CommitDescriptorsForDispatch(CommandList& commandList);

private:
	void CommitDescriptorTables(
		CommandList& commandList,
		std::function< void(ID3D12GraphicsCommandList*, u32, D3D12_GPU_DESCRIPTOR_HANDLE) > setFunc);

public:
	ID3D12DescriptorHeap* GetD3D12DescriptorHeap() const;

private:
	RenderContext& m_RenderContext;

	u32 m_NumDescriptors = 0;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;

	DescriptorPool* m_pDescriptorPool = nullptr;

	u64 m_DescriptorTableBitMask = 0;
	u64 m_DescriptorTableDirtyFlags = 0;
	DescriptorAllocation m_CachedDescriptorAllocations[MAX_ROOT_INDEX] = {};
};

}