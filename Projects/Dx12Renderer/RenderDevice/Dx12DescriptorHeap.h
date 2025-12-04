#pragma once
#include "Dx12RootSignature.h"
#include "Dx12DescriptorAllocation.h"

namespace dx12
{
	
class DescriptorPool;

class Dx12DescriptorHeap
{
public:
	Dx12DescriptorHeap(Dx12RenderDevice& rd, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 maxDescriptors = 1024);
	~Dx12DescriptorHeap();

	void Reset();
	void ParseRootSignature(Arc< Dx12RootSignature > pRootSignature);
	void ParseGlobalRootSignature(Arc< Dx12RootSignature > pRootSignature);

	u32 StageDescriptor(u32 rootIndex, u32 numDescriptors, u32 offset, D3D12_CPU_DESCRIPTOR_HANDLE srcHandle);
	u32 StageDescriptors(u32 rootIndex, u32 offset, std::vector< D3D12_CPU_DESCRIPTOR_HANDLE >&& srcHandles);

	void CommitDescriptorsForDraw(ID3D12GraphicsCommandList2* d3d12CommandList2);
	void CommitDescriptorsForDispatch(ID3D12GraphicsCommandList2* d3d12CommandList2);

	ID3D12DescriptorHeap* GetD3D12DescriptorHeap() const;

private:
	Dx12RenderDevice& m_RenderDevice;

	u32 m_NumDescriptors = 0;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;

	DescriptorPool* m_pDescriptorPool = nullptr;

	u64 m_DescriptorTableBitMask    = 0;
	u64 m_DescriptorTableDirtyFlags = 0;
	std::unordered_map< Dx12RootSignature*, std::array< DescriptorAllocation, MAX_ROOT_INDEX > > m_CachedDescriptorAllocations = {};

	Arc< Dx12RootSignature > m_pCurrentRS = nullptr;
};

}