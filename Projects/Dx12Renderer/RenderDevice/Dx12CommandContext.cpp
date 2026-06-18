#include "RendererPch.h"
#include "Dx12CommandContext.h"
#include "Dx12RenderDevice.h"
#include "Dx12CommandQueue.h"
#include "Dx12BufferAllocator.h"
#include "Dx12RootSignature.h"
#include "Dx12RenderPipeline.h"
#include "Dx12DescriptorPool.h"
#include "Dx12ResourceManager.h"
#include "Dx12Timer.h"

#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12RenderTarget.h"
#include "RenderResource/Dx12SceneResource.h"
#include "RenderResource/Dx12RenderTarget.h"
#include "RenderResource/Dx12ShaderBindingTable.h"
#include "RenderResource/Dx12AccelerationStructure.h"
#include "Utils/Math.hpp"

namespace dx12
{
	
//-------------------------------------------------------------------------
// Impl
//-------------------------------------------------------------------------
class Dx12CommandContext::Impl
{
public:
	Impl(Dx12RenderDevice& rd, const Dx12CommandQueue& cq, D3D12_COMMAND_LIST_TYPE type);
	~Impl();

	void Open();
	void Close();

	// ---- Barrier ----
	void TransitionBarrier(Dx12Resource* pResource, const BarrierState& stateAfter, u32 subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool bFlushImmediate = true);
	void UAVBarrier(Dx12Resource* pResource, bool bFlushImmediate = false);
	void AliasingBarrier(Dx12Resource* pResourceBefore, Dx12Resource* pResourceAfter, bool bFlushImmediate = false);

	// ---- Copy ----
	void UploadData(const Arc< Dx12Buffer >& pDstBuffer, const void* pData, u32 numElements, u64 elemSizeInBytes, u64 dstOffsetInBytes);

	void CopyBuffer(const Arc< Dx12Buffer >& pDstBuffer, const Arc< Dx12Buffer >& pSrcBuffer, size_t sizeInBytes, SIZE_T dstOffsetInBytes, size_t srcOffsetInBytes);
	void CopyBuffer(ID3D12Resource2* d3d12DstBuffer, ID3D12Resource2* d3d12SrcBuffer, SIZE_T sizeInBytes, SIZE_T dstOffsetInBytes, SIZE_T srcOffsetInBytes);
	void CopyTexture(const Arc< Dx12Texture >& pDstTexture, const Arc< Dx12Texture >& pSrcTexture);
	void ResolveSubresource(Dx12Resource* pDstResource, Dx12Resource* pSrcResource, u32 dstSubresource = 0, u32 srcSubresource = 0);

	// ---- Acceleration Structure ----
	void BuildBLAS(Dx12BottomLevelAS& pBLAS);
	void BuildTLAS(Dx12TopLevelAS& pTLAS);

	// ---- Clear ----
	void ClearTexture(const Arc< Dx12Texture >& pTexture, const BarrierState& stateAfter);
	void ClearRenderTarget(const Arc< Dx12Texture >& pTexture);
	void ClearDepthStencil(const Arc< Dx12Texture >& pTexture, D3D12_CLEAR_FLAGS clearFlags);
	void ClearUnorderedAccess(const Arc< Dx12Buffer >& pBuffer, u64 offsetInBytes);
	void ClearUnorderedAccess(const Arc< Dx12Texture >& pTexture);

	// ---- Pipeline ----
	void SetRenderPipeline(Dx12GraphicsPipeline* pGraphicsPipelineState);
	void SetRenderPipeline(Dx12ComputePipeline* pComputePipelineState);
	void SetRenderPipeline(Dx12RaytracingPipeline* pRaytracingPipelineState);

	// ---- Render Target ----
	void SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology);

	void SetViewport(const D3D12_VIEWPORT& viewport);
	void SetViewports(const std::vector< D3D12_VIEWPORT >& viewports);

	void SetScissorRect(const D3D12_RECT& scissorRect);
	void SetScissorRects(const std::vector< D3D12_RECT >& scissorRects);

	void SetRenderTarget(u32 numRenderTargets, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv = D3D12_CPU_DESCRIPTOR_HANDLE());
	void BeginRenderPass(Arc< Dx12RenderTarget > pRenderTarget);

	// ---- Bindings ----
	void SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps);

	void SetGraphicsRootConstant(u32 rootIdx, u32 srcValue, u32 dstOffset = 0);
	void SetGraphicsRootConstants(u32 srcSizeInBytes, const void* pSrcData, u32 dstOffsetInBytes = 0);
	void SetComputeRootConstant(u32 rootIdx, u32 srcValue, u32 dstOffset = 0);
	void SetComputeRootConstants(u32 srcSizeInBytes, const void* pSrcData, u32 dstOffsetInBytes = 0);

	void SetGraphicsDynamicConstantBuffer(const std::string& name, size_t sizeInBytes, const void* pData);
	template< typename T >
	void SetGraphicsDynamicConstantBuffer(const std::string& name, const T& data)
	{
		SetGraphicsDynamicConstantBuffer(name, sizeof(T), &data);
	}
	void SetComputeDynamicConstantBuffer(const std::string& name, size_t sizeInBytes, const void* pData);
	template< typename T >
	void SetComputeDynamicConstantBuffer(const std::string& name, const T& data)
	{
		SetComputeDynamicConstantBuffer(name, sizeof(T), &data);
	}

	void SetGraphicsConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetGraphicsShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetComputeConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetComputeShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetComputeUnorderedAccessView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);

	void SetAccelerationStructureSRV(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);

	void StageDescriptor(
		const std::string& name,
		Arc< Dx12StructuredBuffer > pBuffer,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	void StageDescriptor(
		const std::string& name,
		Arc< Dx12Texture > pTexture,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	void StageDescriptorMip(
		const std::string& name,
		Arc< Dx12Texture > pTexture,
		u32 mipLevel,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	void StageDescriptor(const std::string& name, u32 heapIdx, D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	void StageDescriptors(
		std::vector< std::pair< std::string, u32 > >&& srcHandles,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// ---- Draw / Dispatch ----
	void Draw(u32 vertexCount, u32 instanceCount = 1, u32 startVertex = 0, u32 startInstance = 0);
	void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 startIndex = 0, u32 baseVertex = 0, u32 startInstance = 0);
	void DrawIndirect(const Arc< Dx12Buffer >& pArgumentBuffer, u64 offsetInBytes, u32 numDraws);
	void DrawIndirectWithCount(const Arc< Dx12Buffer >& pArgumentBuffer, u64 offsetInBytes, const Arc< Dx12Buffer >& pCountBuffer, u32 numDraws);

	void Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ);
	void DispatchRays(Dx12ShaderBindingTable& sbt, u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ);

	bool IsComputeContext() const { return m_pComputePipeline != nullptr; }
	bool IsGraphicsContext() const { return m_pGraphicsPipeline != nullptr; }
	bool IsRaytracingContext() const { return m_pRaytracingPipeline != nullptr; }

	double GetLastFrameElapsedTime() const;

	void BeginGpuMarker(const char* name, bool bWithStats);
	void EndGpuMarker();
	const std::vector< render::GpuProfileEntry >& GetLastFrameProfile() const;

private:
	void AddTextureBarrier(const D3D12_TEXTURE_BARRIER& barrier, bool bFlushImmediate);
	void AddBufferBarrier(const D3D12_BUFFER_BARRIER& barrier, bool bFlushImmediate);
	void AddGlobalBarrier(const D3D12_GLOBAL_BARRIER& barrier, bool bFlushImmediate);
	void FlushBarriers();

	void BindDescriptorHeaps();

public:
	D3D12_COMMAND_LIST_TYPE GetCommandListType() const { return m_Type; }
	ID3D12GraphicsCommandList10* GetD3D12CommandList() const { return m_d3d12CommandList10; }

	template< typename T >
	constexpr T RoundUpAndDivide(T Value, size_t Alignment)
	{
		return (T)((Value + Alignment - 1) / Alignment);
	}

private:
	Dx12RenderDevice& m_RenderDevice;
	D3D12_COMMAND_LIST_TYPE m_Type = {};

	Box< DynamicBufferAllocator > m_pConstantBufferPool;
	Box< DynamicBufferAllocator > m_pStagingBufferPool;

	ID3D12GraphicsCommandList10* m_d3d12CommandList10    = nullptr;
	ID3D12CommandAllocator*      m_d3d12CommandAllocator = nullptr;

	Arc< Dx12RootSignature > m_pRootSignature;

	Dx12GraphicsPipeline*   m_pGraphicsPipeline   = nullptr;
	Dx12ComputePipeline*    m_pComputePipeline    = nullptr;
	Dx12RaytracingPipeline* m_pRaytracingPipeline = nullptr;

	D3D_PRIMITIVE_TOPOLOGY m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	//Dx12DescriptorHeap*   m_pDescriptorHeaps[kNumResourceDescriptorType]               = {};
	//ID3D12DescriptorHeap* m_CurrentDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

	u32                   m_NumTextureBarriers = 0;
	D3D12_TEXTURE_BARRIER m_TextureBarriers[kMaxNumPendingBarriers] = {};

	u32                   m_NumBufferBarriers = 0;
	D3D12_BUFFER_BARRIER  m_BufferBarriers[kMaxNumPendingBarriers] = {};

	u32                   m_NumGlobalBarriers = 0;
	D3D12_GLOBAL_BARRIER  m_GlobalBarriers[kMaxNumPendingBarriers] = {};

	Dx12Timer m_Timer = {};

	static Arc< Dx12Buffer > s_pZeroBuffer;
	static constexpr SIZE_T kZeroBufferSize = 4096; // 4KB — enough for most clear operations in a single copy
};
Arc< Dx12Buffer > Dx12CommandContext::Impl::s_pZeroBuffer;

Dx12CommandContext::Impl::Impl(Dx12RenderDevice& rd, const Dx12CommandQueue& cq, D3D12_COMMAND_LIST_TYPE type)
	: m_RenderDevice(rd)
	, m_Type(type)
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	ThrowIfFailed(d3d12Device->CreateCommandAllocator(m_Type, IID_PPV_ARGS(&m_d3d12CommandAllocator)));
	ThrowIfFailed(d3d12Device->CreateCommandList1(0, m_Type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_d3d12CommandList10)));

	m_pConstantBufferPool = MakeBox< DynamicBufferAllocator >(m_RenderDevice);
	m_pStagingBufferPool  = MakeBox< DynamicBufferAllocator >(m_RenderDevice, _MB(128));

	// **
	// Set Gpu Timer (multi-scope profiler)
	// **
	m_Timer.Init(m_RenderDevice.GetD3D12Device(), cq.GetD3D12CommandQueue(), 128);


	// **
	// Zero Buffer for DrawCount
	// **
	if (!s_pZeroBuffer)
	{
		auto desc      = CD3DX12_RESOURCE_DESC1::Buffer(kZeroBufferSize);
		auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

		s_pZeroBuffer = Dx12Buffer::Create(m_RenderDevice, "ZeroBuffer", 
			{
				.count              = 1,
				.elementSizeInBytes = kZeroBufferSize,
				.bufferUsage        = render::eBufferUsage_TransferSource
			});
	}
}

Dx12CommandContext::Impl::~Impl()
{
	m_Timer.Destroy();

	COM_RELEASE(m_d3d12CommandList10);
	COM_RELEASE(m_d3d12CommandAllocator);

	s_pZeroBuffer.reset();
}

void Dx12CommandContext::Impl::Open()
{
	m_pGraphicsPipeline   = nullptr;
	m_pComputePipeline    = nullptr;
	m_pRaytracingPipeline = nullptr;

	m_pRootSignature    = nullptr;
	m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	ThrowIfFailed(m_d3d12CommandAllocator->Reset());
	ThrowIfFailed(m_d3d12CommandList10->Reset(m_d3d12CommandAllocator, nullptr));

	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
	{
		m_pConstantBufferPool->Reset();
		m_pStagingBufferPool->Reset();

		BindDescriptorHeaps();

		auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
		m_d3d12CommandList10->SetComputeRootSignature(rm.GetGlobalRootSignature()->GetD3D12RootSignature());
		m_d3d12CommandList10->SetGraphicsRootSignature(rm.GetGlobalRootSignature()->GetD3D12RootSignature());

		// Reads previous frame's resolved timestamps and opens the implicit "Frame" scope.
		m_Timer.BeginFrame(m_d3d12CommandList10);
	}
}

void Dx12CommandContext::Impl::Close()
{
	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
		m_Timer.EndFrame(m_d3d12CommandList10); // closes "Frame" + ResolveQueryData

	FlushBarriers();
	m_d3d12CommandList10->Close();
}


// =========================================================================
// Impl - Barrier Operations
// =========================================================================
void Dx12CommandContext::Impl::TransitionBarrier(Dx12Resource* pResource, const BarrierState& stateAfter, u32 subresource, bool bFlushImmediate)
{
	if (!pResource)
		return;

	if (pResource->IsTexture() &&
		pResource->GetCurrentState().HasIndividualSubresources() &&
		subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
	{
		for (const auto& [sub, subState] : pResource->GetCurrentState())
		{
			if (subState == stateAfter)
				continue;

			D3D12_TEXTURE_BARRIER texBarrier = {};
			texBarrier.SyncBefore   = subState.Sync;
			texBarrier.SyncAfter    = stateAfter.Sync;
			texBarrier.AccessBefore = subState.Access;
			texBarrier.AccessAfter  = stateAfter.Access;
			texBarrier.LayoutBefore = subState.Layout;
			texBarrier.LayoutAfter  = stateAfter.Layout;
			texBarrier.pResource    = pResource->GetD3D12Resource();
			texBarrier.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(sub);
			texBarrier.Flags        = D3D12_TEXTURE_BARRIER_FLAG_NONE;

			AddTextureBarrier(texBarrier, false);
		}

		pResource->SetCurrentState(stateAfter, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

		if (bFlushImmediate)
			FlushBarriers();

		return;
	}

	const auto& stateBefore = pResource->GetCurrentState().GetSubresourceState(subresource);
	if (stateBefore == stateAfter)
	{
		if (bFlushImmediate)
			FlushBarriers();

		return;
	}

	if (pResource->IsTexture())
	{
		D3D12_TEXTURE_BARRIER texBarrier = {};
		texBarrier.SyncBefore   = stateBefore.Sync;
		texBarrier.SyncAfter    = stateAfter.Sync;
		texBarrier.AccessBefore = stateBefore.Access;
		texBarrier.AccessAfter  = stateAfter.Access;
		texBarrier.LayoutBefore = stateBefore.Layout;
		texBarrier.LayoutAfter  = stateAfter.Layout;
		texBarrier.pResource    = pResource->GetD3D12Resource();
		texBarrier.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(subresource);
		texBarrier.Flags        = D3D12_TEXTURE_BARRIER_FLAG_NONE;

		AddTextureBarrier(texBarrier, bFlushImmediate);
	}
	else if (pResource->IsBuffer())
	{
		D3D12_BUFFER_BARRIER barrier = {};
		barrier.SyncBefore   = stateBefore.Sync;
		barrier.SyncAfter    = stateAfter.Sync;
		barrier.AccessBefore = stateBefore.Access;
		barrier.AccessAfter  = stateAfter.Access;
		barrier.pResource    = pResource->GetD3D12Resource();
		barrier.Offset       = 0;
		barrier.Size         = UINT64_MAX;

		AddBufferBarrier(barrier, bFlushImmediate);
	}
	else
	{
		assert(false && "Invalid resource type for barrier");
	}

	pResource->SetCurrentState(stateAfter, subresource);
}

void Dx12CommandContext::Impl::UAVBarrier(Dx12Resource* pResource, bool bFlushImmediate)
{
	if (!pResource)
		return;

	if (pResource->IsTexture())
	{
		D3D12_TEXTURE_BARRIER texBarrier = {};
		texBarrier.SyncBefore   = D3D12_BARRIER_SYNC_ALL_SHADING;
		texBarrier.SyncAfter    = D3D12_BARRIER_SYNC_ALL_SHADING;
		texBarrier.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		texBarrier.AccessAfter  = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		texBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
		texBarrier.LayoutAfter  = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
		texBarrier.pResource    = pResource->GetD3D12Resource();
		texBarrier.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		texBarrier.Flags        = D3D12_TEXTURE_BARRIER_FLAG_NONE;

		AddTextureBarrier(texBarrier, bFlushImmediate);
	}
	else
	{
		D3D12_BUFFER_BARRIER bufBarrier = {};
		bufBarrier.SyncBefore   = D3D12_BARRIER_SYNC_ALL_SHADING;
		bufBarrier.SyncAfter    = D3D12_BARRIER_SYNC_ALL_SHADING;
		bufBarrier.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		bufBarrier.AccessAfter  = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		bufBarrier.pResource    = pResource->GetD3D12Resource();
		bufBarrier.Offset       = 0;
		bufBarrier.Size         = UINT64_MAX;

		AddBufferBarrier(bufBarrier, bFlushImmediate);
	}
}

void Dx12CommandContext::Impl::AliasingBarrier(Dx12Resource* pResourceBefore, Dx12Resource* pResourceAfter, bool bFlushImmediate)
{
	if (!pResourceBefore || !pResourceAfter)
		return;

	// Enhanced Barriers: aliasing is expressed as a global barrier with SYNC_ALL and ACCESS_NO_ACCESS on both sides.
	// The runtime deduces aliasing from the heap overlap.
	D3D12_GLOBAL_BARRIER globalBarrier = {};
	globalBarrier.SyncBefore   = D3D12_BARRIER_SYNC_ALL;
	globalBarrier.SyncAfter    = D3D12_BARRIER_SYNC_ALL;
	globalBarrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
	globalBarrier.AccessAfter  = D3D12_BARRIER_ACCESS_NO_ACCESS;

	AddGlobalBarrier(globalBarrier, bFlushImmediate);
}


// =========================================================================
// Impl - Copy Operations
// =========================================================================
void Dx12CommandContext::Impl::UploadData(const Arc< Dx12Buffer >& pDstBuffer, const void* pData, u32 numElements, u64 elemSizeInBytes, u64 dstOffsetInBytes)
{
	u64 sizeInBytes = numElements * elemSizeInBytes;

	auto allocation = m_pStagingBufferPool->Allocate(sizeInBytes);
	memcpy(allocation.CPUHandle, pData, sizeInBytes);

	CopyBuffer(pDstBuffer, allocation.pBuffer, sizeInBytes, dstOffsetInBytes, allocation.offsetInBytes);
}

void Dx12CommandContext::Impl::CopyBuffer(const Arc< Dx12Buffer >& pDstBuffer, const Arc< Dx12Buffer >& pSrcBuffer, size_t sizeInBytes, size_t dstOffsetInBytes, size_t srcOffsetInBytes)
{
	if (!pDstBuffer->IsMapped())
		TransitionBarrier(pDstBuffer.get(), BarrierStates::CopyDest);
	if (!pSrcBuffer->IsMapped())
		TransitionBarrier(pSrcBuffer.get(), BarrierStates::CopySource);

	CopyBuffer(pDstBuffer->GetD3D12Resource(), pSrcBuffer->GetD3D12Resource(), sizeInBytes, dstOffsetInBytes, srcOffsetInBytes);
}

void Dx12CommandContext::Impl::CopyBuffer(ID3D12Resource2* d3d12DstBuffer, ID3D12Resource2* d3d12SrcBuffer, SIZE_T sizeInBytes, SIZE_T dstOffsetInBytes, SIZE_T srcOffsetInBytes)
{
	m_d3d12CommandList10->CopyBufferRegion(d3d12DstBuffer, dstOffsetInBytes, d3d12SrcBuffer, srcOffsetInBytes, sizeInBytes);
}

void Dx12CommandContext::Impl::CopyTexture(const Arc< Dx12Texture >& pDstTexture, const Arc< Dx12Texture >& pSrcTexture)
{
	TransitionBarrier(pDstTexture.get(), BarrierStates::CopyDest, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	TransitionBarrier(pSrcTexture.get(), BarrierStates::CopySource);

	D3D12_RESOURCE_DESC1 Desc = pDstTexture->Desc();
	for (u16 i = 0; i < Desc.MipLevels; i++)
	{
		D3D12_TEXTURE_COPY_LOCATION	dstLocation = {};
		dstLocation.pResource        = pDstTexture->GetD3D12Resource();
		dstLocation.SubresourceIndex = i;
		dstLocation.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

		D3D12_TEXTURE_COPY_LOCATION	srcLocation = {};
		srcLocation.pResource        = pSrcTexture->GetD3D12Resource();
		srcLocation.SubresourceIndex = i;
		srcLocation.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

		m_d3d12CommandList10->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
	}
}

void Dx12CommandContext::Impl::ResolveSubresource(Dx12Resource* pDstResource, Dx12Resource* pSrcResource, u32 dstSubresource, u32 srcSubresource)
{
	if (pDstResource && pSrcResource)
	{
		TransitionBarrier(pDstResource, BarrierStates::ResolveDest, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		TransitionBarrier(pSrcResource, BarrierStates::ResolveSource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);

		FlushBarriers();

		m_d3d12CommandList10->ResolveSubresource(pDstResource->GetD3D12Resource(), dstSubresource,
			pSrcResource->GetD3D12Resource(), srcSubresource, pDstResource->Desc().Format);
	}
}


// =========================================================================
// Impl - Acceleration Structure
// =========================================================================
void Dx12CommandContext::Impl::BuildBLAS(Dx12BottomLevelAS& BLAS)
{
	FlushBarriers();

	const auto& buildInputs = BLAS.GetBuildInputs();

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs                           = buildInputs;
	buildDesc.DestAccelerationStructureData    = BLAS.GetResultBuffer()->GetGPUVirtualAddress();
	buildDesc.ScratchAccelerationStructureData = BLAS.GetScratchBuffer()->GetGPUVirtualAddress();
	buildDesc.SourceAccelerationStructureData  = 0;

	m_d3d12CommandList10->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	D3D12_BUFFER_BARRIER uavBarrier = {};
	uavBarrier.SyncBefore   = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
	uavBarrier.SyncAfter    = D3D12_BARRIER_SYNC_RAYTRACING;
	uavBarrier.AccessBefore = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
	uavBarrier.AccessAfter  = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
	uavBarrier.pResource    = BLAS.GetResultBuffer();
	uavBarrier.Offset       = 0;
	uavBarrier.Size         = UINT64_MAX;
	AddBufferBarrier(uavBarrier, true);

	BLAS.MarkBuilt();
}

void Dx12CommandContext::Impl::BuildTLAS(Dx12TopLevelAS& TLAS)
{
	FlushBarriers();

	const auto& buildInputs = TLAS.GetBuildInputs();

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs                           = buildInputs;
	buildDesc.DestAccelerationStructureData    = TLAS.GetResultBuffer()->GetGPUVirtualAddress();
	buildDesc.ScratchAccelerationStructureData = TLAS.GetScratchBuffer()->GetGPUVirtualAddress();
	buildDesc.SourceAccelerationStructureData  = 0;

	m_d3d12CommandList10->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	D3D12_BUFFER_BARRIER uavBarrier = {};
	uavBarrier.SyncBefore   = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
	uavBarrier.SyncAfter    = D3D12_BARRIER_SYNC_RAYTRACING;
	uavBarrier.AccessBefore = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
	uavBarrier.AccessAfter  = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
	uavBarrier.pResource    = TLAS.GetResultBuffer();
	uavBarrier.Offset       = 0;
	uavBarrier.Size         = UINT64_MAX;
	AddBufferBarrier(uavBarrier, true);

	TLAS.MarkBuilt();
}


// =========================================================================
// Impl - Clear
// =========================================================================
void Dx12CommandContext::Impl::ClearTexture(const Arc< Dx12Texture >& pTexture, const BarrierState& stateAfter)
{
	if (stateAfter.Layout == D3D12_BARRIER_LAYOUT_RENDER_TARGET)
	{
		if (pTexture->GetRenderTargetView().ptr == 0)
		{
			__debugbreak();
		}

		ClearRenderTarget(pTexture);
	}
	else if (stateAfter.Layout == D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE)
	{
		if (pTexture->GetDepthStencilView().ptr == 0)
		{
			__debugbreak();
		}

		ClearDepthStencil(pTexture, D3D12_CLEAR_FLAG_DEPTH);
	}
	else if (stateAfter.Layout == D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS)
	{
		if (pTexture->GetUnorderedAccessView(0).ptr == 0)
		{
			__debugbreak();
		}

		ClearUnorderedAccess(pTexture);
	}
}

void Dx12CommandContext::Impl::ClearRenderTarget(const Arc< Dx12Texture >& pTexture)
{
	assert(pTexture);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	if (auto pClearValue = pTexture->GetClearValue())
	{
		memcpy(clearColor, pClearValue->Color, sizeof(clearColor));
	}

	TransitionBarrier(pTexture.get(), BarrierStates::RenderTarget);
	m_d3d12CommandList10->ClearRenderTargetView(pTexture->GetRenderTargetView(), clearColor, 0, nullptr);
}

void Dx12CommandContext::Impl::ClearDepthStencil(const Arc< Dx12Texture >& pTexture, D3D12_CLEAR_FLAGS clearFlags)
{
	assert(pTexture);
	float clearDepth   = 1.0f;
	u8    clearStencil = 0;
	if (auto pClearValue = pTexture->GetClearValue())
	{
		clearDepth   = pTexture->GetClearValue()->DepthStencil.Depth;
		clearStencil = pTexture->GetClearValue()->DepthStencil.Stencil;
	}

	TransitionBarrier(pTexture.get(), BarrierStates::DepthStencilWrite);
	m_d3d12CommandList10->ClearDepthStencilView(pTexture->GetDepthStencilView(), clearFlags, clearDepth, clearStencil, 0, nullptr);
}

void Dx12CommandContext::Impl::ClearUnorderedAccess(const Arc< Dx12Buffer >& pBuffer, u64 offsetInBytes)
{
	assert(s_pZeroBuffer);

	TransitionBarrier(pBuffer.get(), BarrierStates::BufferCopyDest);
	TransitionBarrier(s_pZeroBuffer.get(), BarrierStates::BufferCopySource);

	// Copy in chunks for buffers larger than the zero buffer
	u64 remaining = pBuffer->SizeInBytes() - offsetInBytes;
	u64 dstOffset = offsetInBytes;
	while (remaining > 0)
	{
		u64 chunkSize = std::min(remaining, static_cast<u64>(kZeroBufferSize));

		CopyBuffer(pBuffer, s_pZeroBuffer, chunkSize, dstOffset, 0);
		dstOffset += chunkSize;
		remaining -= chunkSize;
	}
}

void Dx12CommandContext::Impl::ClearUnorderedAccess(const Arc< Dx12Texture >& pTexture)
{
	assert(pTexture);
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	if (auto pClearValue = pTexture->GetClearValue())
	{
		memcpy(clearColor, pClearValue->Color, sizeof(clearColor));
	}

	D3D12_RECT rect = {};
	rect.left = 0; rect.bottom = 0;
	rect.right = static_cast<LONG>(pTexture->GetWidth()); rect.top = static_cast<LONG>(pTexture->GetHeight());

	TransitionBarrier(pTexture.get(), BarrierStates::UnorderedAccess);
	m_d3d12CommandList10->ClearUnorderedAccessViewFloat(pTexture->GetUnorderedAccessGpuAddress(0), pTexture->GetUnorderedAccessView(0), pTexture->GetD3D12Resource(), clearColor, 1, &rect);
}


// =========================================================================
// Impl - Render Pipeline
// =========================================================================
void Dx12CommandContext::Impl::SetRenderPipeline(Dx12GraphicsPipeline* pGraphicsPipeline)
{
	m_pComputePipeline    = nullptr;
	m_pRaytracingPipeline = nullptr;
	if (m_pGraphicsPipeline != pGraphicsPipeline)
	{
		m_pGraphicsPipeline = pGraphicsPipeline;

		const auto& pRootSignature = m_pGraphicsPipeline->GetRootSignature();
		if (m_pRootSignature != pRootSignature)
		{
			m_pRootSignature = pRootSignature;

			m_d3d12CommandList10->SetGraphicsRootSignature(m_pRootSignature->GetD3D12RootSignature());
		}

		m_d3d12CommandList10->SetPipelineState(m_pGraphicsPipeline->GetD3D12PipelineState());

		const D3D_PRIMITIVE_TOPOLOGY topology = m_pGraphicsPipeline->GetD3D12PrimitiveTopology();
		if (topology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED && m_PrimitiveTopology != topology)
		{
			m_PrimitiveTopology = topology;
			m_d3d12CommandList10->IASetPrimitiveTopology(topology);
		}
	}
}

void Dx12CommandContext::Impl::SetRenderPipeline(Dx12ComputePipeline* pComputePipeline)
{
	m_pGraphicsPipeline   = nullptr;
	m_pRaytracingPipeline = nullptr;
	if (m_pComputePipeline != pComputePipeline)
	{
		m_pComputePipeline = pComputePipeline;

		const auto& pRootSignature = m_pComputePipeline->GetRootSignature();
		if (m_pRootSignature != pRootSignature)
		{
			m_pRootSignature = pRootSignature;

			m_d3d12CommandList10->SetComputeRootSignature(m_pRootSignature->GetD3D12RootSignature());
		}

		m_d3d12CommandList10->SetPipelineState(m_pComputePipeline->GetD3D12PipelineState());
	}
}

void Dx12CommandContext::Impl::SetRenderPipeline(Dx12RaytracingPipeline* pRaytracingPipelineState)
{
	m_pComputePipeline  = nullptr;
	m_pGraphicsPipeline = nullptr;
	if (m_pRaytracingPipeline != pRaytracingPipelineState)
	{
		m_pRaytracingPipeline = pRaytracingPipelineState;

		const auto& pRootSignature = m_pRaytracingPipeline->GetGlobalRootSignature();
		if (m_pRootSignature != pRootSignature)
		{
			m_pRootSignature = pRootSignature;

			m_d3d12CommandList10->SetComputeRootSignature(m_pRootSignature->GetD3D12RootSignature());
		}

		m_d3d12CommandList10->SetPipelineState1(m_pRaytracingPipeline->GetD3D12StateObject());
	}
}


// =========================================================================
// Impl - Render Target
// =========================================================================
void Dx12CommandContext::Impl::SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology)
{
	assert(m_pGraphicsPipeline);

	if (m_PrimitiveTopology != primitiveTopology)
	{
		m_PrimitiveTopology = primitiveTopology;
		m_d3d12CommandList10->IASetPrimitiveTopology(m_PrimitiveTopology);
	}
}

void Dx12CommandContext::Impl::SetViewport(const D3D12_VIEWPORT& viewport)
{
	SetViewports({ viewport });
}

void Dx12CommandContext::Impl::SetViewports(const std::vector< D3D12_VIEWPORT >& viewports)
{
	assert(viewports.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
	m_d3d12CommandList10->RSSetViewports(static_cast<u32>(viewports.size()), viewports.data());
}

void Dx12CommandContext::Impl::SetScissorRect(const D3D12_RECT& scissorRect)
{
	SetScissorRects({ scissorRect });
}

void Dx12CommandContext::Impl::SetScissorRects(const std::vector< D3D12_RECT >& scissorRects)
{
	assert(scissorRects.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
	m_d3d12CommandList10->RSSetScissorRects(static_cast<u32>(scissorRects.size()), scissorRects.data());
}

void Dx12CommandContext::Impl::SetRenderTarget(u32 numRenderTargets, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
	if (dsv.ptr == 0)
	{
		m_d3d12CommandList10->OMSetRenderTargets(numRenderTargets, &rtv, FALSE, nullptr);
	}
	else
	{
		m_d3d12CommandList10->OMSetRenderTargets(numRenderTargets, &rtv, FALSE, &dsv);
	}
}

void Dx12CommandContext::Impl::BeginRenderPass(Arc< Dx12RenderTarget > pRenderTarget)
{
	using namespace render;
	std::vector< D3D12_CPU_DESCRIPTOR_HANDLE > d3d12RenderTargetDescriptors;
	d3d12RenderTargetDescriptors.reserve(eAttachmentPoint::NumAttachmentPoints);

	const auto& pTextures = pRenderTarget->GetAttachments();
	for (auto i = 0; i < eAttachmentPoint::DepthStencil; ++i)
	{
		auto rhiTexture = StaticCast<Dx12Texture>(pTextures[i]);
		if (rhiTexture)
		{
			TransitionBarrier(rhiTexture.get(), BarrierStates::RenderTarget);
			d3d12RenderTargetDescriptors.push_back(rhiTexture->GetRenderTargetView());
		}
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor(D3D12_DEFAULT);
	auto rhiDepthTexture = StaticCast<Dx12Texture>(pRenderTarget->Attachment(eAttachmentPoint::DepthStencil));
	if (rhiDepthTexture)
	{
		TransitionBarrier(rhiDepthTexture.get(), BarrierStates::DepthStencilWrite);
		depthStencilDescriptor = rhiDepthTexture->GetDepthStencilView();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE* d3d12DSV = depthStencilDescriptor.ptr != 0 ? &depthStencilDescriptor : nullptr;

	SetViewport(pRenderTarget->GetViewport());
	SetScissorRect(pRenderTarget->GetScissorRect());
	m_d3d12CommandList10->OMSetRenderTargets(
		static_cast<u32>(d3d12RenderTargetDescriptors.size()),
		d3d12RenderTargetDescriptors.data(), 
		FALSE, 
		d3d12DSV
	);
}


// =========================================================================
// Impl - Shader Bindings
// =========================================================================
void Dx12CommandContext::Impl::SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps)
{
	m_d3d12CommandList10->SetDescriptorHeaps(static_cast<u32>(d3d12DescriptorHeaps.size()), d3d12DescriptorHeaps.data());
}

void Dx12CommandContext::Impl::SetGraphicsRootConstant(u32 rootIdx, u32 srcValue, u32 dstOffset)
{
	m_d3d12CommandList10->SetGraphicsRoot32BitConstant(rootIdx, srcValue, dstOffset);
}

void Dx12CommandContext::Impl::SetGraphicsRootConstants(u32 srcSizeInBytes, const void* pSrcData, u32 dstOffsetInBytes)
{
	u32 size      = srcSizeInBytes / 4;
	u32 dstOffset = dstOffsetInBytes / 4;

	assert(m_pRootSignature);
	const u32 rootIndex = m_pRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, kRootConstantSpace, 0);
	assert(rootIndex != kInvalidIndex);
	m_d3d12CommandList10->SetGraphicsRoot32BitConstants(rootIndex, size, pSrcData, dstOffset);
}

void Dx12CommandContext::Impl::SetComputeRootConstant(u32 rootIdx, u32 srcValue, u32 dstOffset)
{
	m_d3d12CommandList10->SetComputeRoot32BitConstant(rootIdx, srcValue, dstOffset);
}

void Dx12CommandContext::Impl::SetComputeRootConstants(u32 srcSizeInBytes, const void* pSrcData, u32 dstOffsetInBytes)
{
	u32 size      = srcSizeInBytes / 4;
	u32 dstOffset = dstOffsetInBytes / 4;

	assert(m_pRootSignature);
	const u32 rootIndex = m_pRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, kRootConstantSpace, 0);
	assert(rootIndex != kInvalidIndex);
	m_d3d12CommandList10->SetComputeRoot32BitConstants(rootIndex, size, pSrcData, dstOffset);
}

void Dx12CommandContext::Impl::SetGraphicsDynamicConstantBuffer(const std::string& name, size_t sizeInBytes, const void* pData)
{
	auto allocation = m_pConstantBufferPool->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	memcpy(allocation.CPUHandle, pData, sizeInBytes);

	auto [_, rootIndex] = m_pGraphicsPipeline->GetResourceBindingIndex(name);
	if (rootIndex == kInvalidIndex)
	{
		return;
	}

	m_d3d12CommandList10->SetGraphicsRootConstantBufferView(rootIndex, allocation.GPUHandle);
}

void Dx12CommandContext::Impl::SetComputeDynamicConstantBuffer(const std::string& name, size_t sizeInBytes, const void* pData)
{
	auto allocation = m_pConstantBufferPool->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	memcpy(allocation.CPUHandle, pData, sizeInBytes);

	if (m_pComputePipeline)
	{
		auto [_, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
		if (rootIndex == kInvalidIndex)
		{
			return;
		}

		m_d3d12CommandList10->SetComputeRootConstantBufferView(rootIndex, allocation.GPUHandle);
	}
	else if (m_pRaytracingPipeline)
	{
		auto [_, rootIndex] = m_pRaytracingPipeline->GetResourceBindingIndex(name);
		if (rootIndex == kInvalidIndex)
		{
			return;
		}

		m_d3d12CommandList10->SetComputeRootConstantBufferView(rootIndex, allocation.GPUHandle);
	}
}

void Dx12CommandContext::Impl::SetGraphicsConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	assert(IsGraphicsContext());

	auto [_, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
	if (rootIndex == kInvalidIndex)
	{
		return;
	}

	m_d3d12CommandList10->SetGraphicsRootConstantBufferView(rootIndex, gpuHandle);
}

void Dx12CommandContext::Impl::SetGraphicsShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	auto [_, rootIndex] = m_pGraphicsPipeline->GetResourceBindingIndex(name);
	if (rootIndex == kInvalidIndex)
	{
		return;
	}

	m_d3d12CommandList10->SetGraphicsRootShaderResourceView(rootIndex, gpuHandle);
}

void Dx12CommandContext::Impl::SetComputeConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	auto [_, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
	if (rootIndex == kInvalidIndex)
	{
		return;
	}

	m_d3d12CommandList10->SetComputeRootConstantBufferView(rootIndex, gpuHandle);
}

void Dx12CommandContext::Impl::SetComputeShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	auto [_, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
	if (rootIndex == kInvalidIndex)
	{
		return;
	}

	m_d3d12CommandList10->SetComputeRootShaderResourceView(rootIndex, gpuHandle);
}

void Dx12CommandContext::Impl::SetComputeUnorderedAccessView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	auto [_, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
	if (rootIndex == kInvalidIndex)
	{
		return;
	}

	m_d3d12CommandList10->SetComputeRootUnorderedAccessView(rootIndex, gpuHandle);
}

void Dx12CommandContext::Impl::SetAccelerationStructureSRV(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress)
{
	u32 rootIndex = kInvalidIndex;

	if (IsRaytracingContext())
	{
		auto [_, idx] = m_pRaytracingPipeline->GetResourceBindingIndex(name);
		rootIndex = idx;
	}
	else if (IsComputeContext())
	{
		auto [_, idx] = m_pComputePipeline->GetResourceBindingIndex(name);
		rootIndex = idx;
	}

	if (rootIndex == kInvalidIndex)
	{
		return;
	}

	m_d3d12CommandList10->SetComputeRootShaderResourceView(rootIndex, gpuAddress);
}

void Dx12CommandContext::Impl::StageDescriptor(
	const std::string& name,
	Arc< Dx12StructuredBuffer > pBuffer,
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	const auto& state = pBuffer->GetCurrentState();
	bool bIsUAV = state.GetSubresourceState().Access & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;

	if (bIsUAV)
		StageDescriptor(name, pBuffer->GetUnorderedAccessHandle(), heapType);
	else
		StageDescriptor(name, pBuffer->GetShaderResourceHandle(), heapType);
}

void Dx12CommandContext::Impl::StageDescriptor(
	const std::string& name,
	Arc< Dx12Texture > pTexture,
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	const auto& state = pTexture->GetCurrentState();
	bool bIsUAV = 
		(IsComputeContext() || IsRaytracingContext()) && state.GetSubresourceState() == BarrierStates::UnorderedAccess;

	if (bIsUAV)
	{
		StageDescriptor(name, pTexture->GetUnorderedAccessHandle(), heapType);
	}
	else
	{
		StageDescriptor(name, pTexture->GetShaderResourceHandle(), heapType);
	}
}

void Dx12CommandContext::Impl::StageDescriptorMip(
	const std::string& name,
	Arc< Dx12Texture > pTexture,
	u32 mipLevel,
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	StageDescriptor(name, pTexture->GetUnorderedAccessHandle(mipLevel), heapType);
}

void Dx12CommandContext::Impl::StageDescriptor(const std::string& name, u32 heapIdx, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	if (IsGraphicsContext())
	{
		auto [offset, rootIndex] = m_pGraphicsPipeline->GetResourceBindingIndex(name);
		if (rootIndex == kInvalidIndex)
		{
			return;
		}

		SetGraphicsRootConstant(rootIndex, heapIdx);
	}
	else if (IsComputeContext())
	{
		auto [offset, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
		if (rootIndex == kInvalidIndex)
		{
			return;
		}

		SetComputeRootConstant(rootIndex, heapIdx);
	}
	else if (IsRaytracingContext())
	{
		auto [offset, rootIndex] = m_pRaytracingPipeline->GetResourceBindingIndex(name);
		if (rootIndex == kInvalidIndex)
		{
			return;
		}

		SetComputeRootConstant(rootIndex, heapIdx);
	}
}

void Dx12CommandContext::Impl::StageDescriptors(
	std::vector< std::pair< std::string, u32 > >&& srcHandles,
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	for (const auto& [name, srcHandle] : srcHandles)
	{
		StageDescriptor(name, srcHandle, heapType);
	}
}


// =========================================================================
// Impl - Draw / Dispatch
// =========================================================================
void Dx12CommandContext::Impl::Draw(u32 vertexCount, u32 instanceCount, u32 startVertex, u32 startInstance)
{
	FlushBarriers();

	m_d3d12CommandList10->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
}

void Dx12CommandContext::Impl::DrawIndexed(u32 indexCount, u32 instanceCount, u32 startIndex, u32 baseVertex, u32 startInstance)
{
	FlushBarriers();

	m_d3d12CommandList10->DrawIndexedInstanced(indexCount, instanceCount, startIndex, (i32)baseVertex, startInstance);
}

void Dx12CommandContext::Impl::DrawIndirect(const Arc< Dx12Buffer >& pArgumentBuffer, u64 offsetInBytes, u32 numDraws)
{
	UNUSED(offsetInBytes);
	FlushBarriers();

	auto& sr = static_cast<Dx12SceneResource&>(m_RenderDevice.GetResourceManager().GetSceneResource());
	m_d3d12CommandList10->ExecuteIndirect(
		sr.GetSceneD3D12CommandSignature(),
		numDraws,
		pArgumentBuffer->GetD3D12Resource(),
		0,
		nullptr,
		0
	);
}

void Dx12CommandContext::Impl::DrawIndirectWithCount(const Arc< Dx12Buffer >& pArgumentBuffer, u64 offsetInBytes, const Arc< Dx12Buffer >& pCountBuffer, u32 numDraws)
{
	UNUSED(offsetInBytes);
	FlushBarriers();

	auto& sr = static_cast<Dx12SceneResource&>(m_RenderDevice.GetResourceManager().GetSceneResource());
	m_d3d12CommandList10->ExecuteIndirect(
		sr.GetSceneD3D12CommandSignature(),
		numDraws,
		pArgumentBuffer->GetD3D12Resource(),
		0,
		pCountBuffer->GetD3D12Resource(),
		0
	);
}

void Dx12CommandContext::Impl::Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	FlushBarriers();

	m_d3d12CommandList10->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void Dx12CommandContext::Impl::DispatchRays(Dx12ShaderBindingTable& sbt, u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	FlushBarriers();

	const auto& desc = sbt.GetDispatchRaysDesc(numGroupsX, numGroupsY, numGroupsZ);
	m_d3d12CommandList10->DispatchRays(&desc);
}

void Dx12CommandContext::Impl::BindDescriptorHeaps()
{
	auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
	ID3D12DescriptorHeap* descriptorHeap = rm.GetGlobalDescriptorHeap()->GetD3D12DescriptorHeap();
	ID3D12DescriptorHeap* descriptorHeaps[kNumResourceDescriptorType] = { descriptorHeap, nullptr };

	m_d3d12CommandList10->SetDescriptorHeaps(1, descriptorHeaps);
}

double Dx12CommandContext::Impl::GetLastFrameElapsedTime() const
{
	return m_Timer.GetLastFrameTotalNs();
}

void Dx12CommandContext::Impl::BeginGpuMarker(const char* name, bool bWithStats)
{
	if (m_Type == D3D12_COMMAND_LIST_TYPE_COPY)
		return;
	m_Timer.BeginMarker(m_d3d12CommandList10, name, bWithStats);
}

void Dx12CommandContext::Impl::EndGpuMarker()
{
	if (m_Type == D3D12_COMMAND_LIST_TYPE_COPY)
		return;
	m_Timer.EndMarker(m_d3d12CommandList10);
}

const std::vector< render::GpuProfileEntry >& Dx12CommandContext::Impl::GetLastFrameProfile() const
{
	return m_Timer.GetLastFrameProfile();
}

void Dx12CommandContext::Impl::AddTextureBarrier(const D3D12_TEXTURE_BARRIER& barrier, bool bFlushImmediate)
{
	m_TextureBarriers[m_NumTextureBarriers++] = barrier;

	if (bFlushImmediate || m_NumTextureBarriers == kMaxNumPendingBarriers)
	{
		FlushBarriers();
	}
}

void Dx12CommandContext::Impl::AddBufferBarrier(const D3D12_BUFFER_BARRIER& barrier, bool bFlushImmediate)
{
	m_BufferBarriers[m_NumBufferBarriers++] = barrier;

	if (bFlushImmediate || m_NumBufferBarriers == kMaxNumPendingBarriers)
	{
		FlushBarriers();
	}
}

void Dx12CommandContext::Impl::AddGlobalBarrier(const D3D12_GLOBAL_BARRIER& barrier, bool bFlushImmediate)
{
	m_GlobalBarriers[m_NumGlobalBarriers++] = barrier;

	if (bFlushImmediate || m_NumGlobalBarriers == kMaxNumPendingBarriers)
	{
		FlushBarriers();
	}
}

void Dx12CommandContext::Impl::FlushBarriers()
{
	u32 numGroups = 0;

	D3D12_BARRIER_GROUP groups[3] = {};
	if (m_NumTextureBarriers > 0)
	{
		auto& group = groups[numGroups++];
		group.Type             = D3D12_BARRIER_TYPE_TEXTURE;
		group.NumBarriers      = m_NumTextureBarriers;
		group.pTextureBarriers = m_TextureBarriers;
	}

	if (m_NumBufferBarriers > 0)
	{
		auto& group = groups[numGroups++];
		group.Type            = D3D12_BARRIER_TYPE_BUFFER;
		group.NumBarriers     = m_NumBufferBarriers;
		group.pBufferBarriers = m_BufferBarriers;
	}

	if (m_NumGlobalBarriers > 0)
	{
		auto& group = groups[numGroups++];
		group.Type            = D3D12_BARRIER_TYPE_GLOBAL;
		group.NumBarriers     = m_NumGlobalBarriers;
		group.pGlobalBarriers = m_GlobalBarriers;
	}

	if (numGroups > 0)
	{
		m_d3d12CommandList10->Barrier(numGroups, groups);
	}

	m_NumTextureBarriers = 0;
	m_NumBufferBarriers  = 0;
	m_NumGlobalBarriers  = 0;
}


//-------------------------------------------------------------------------
// Command Context
//-------------------------------------------------------------------------
Dx12CommandContext::Dx12CommandContext(Dx12RenderDevice& rd, const  Dx12CommandQueue& cq, D3D12_COMMAND_LIST_TYPE type)
	: m_Impl(MakeBox< Impl >(rd, cq, type)) 
{
}

void Dx12CommandContext::Open()
{
	m_Impl->Open();
}

void Dx12CommandContext::Close()
{
	m_Impl->Close();
}

void Dx12CommandContext::ClearBuffer(const Arc< render::Buffer >& pBuffer, u32 value, u64 offsetInBytes)
{
	UNUSED(value);

	const auto& rhiBuffer = StaticCast<Dx12Buffer>(pBuffer);
	m_Impl->ClearUnorderedAccess(rhiBuffer, offsetInBytes);
}

void Dx12CommandContext::ClearTexture(const Arc< render::Texture >& pTexture, render::eTextureLayout newLayout)
{
	m_Impl->ClearTexture(StaticCast<Dx12Texture>(pTexture), DX12_BARRIER_STATE(newLayout, true));
}

void Dx12CommandContext::ClearRenderTarget(const Arc< Dx12Texture >& pTexture)
{
	m_Impl->ClearRenderTarget(pTexture);
}

void Dx12CommandContext::ClearDepthStencil(const Arc< Dx12Texture >& pTexture, D3D12_CLEAR_FLAGS clearFlags)
{
	m_Impl->ClearDepthStencil(pTexture, clearFlags);
}

void Dx12CommandContext::TransitionBufferToRead(const Arc< render::Buffer >& pBuffer, render::ePipelineStage dstStage, u64 offsetInBytes, bool bFlushImmediate)
{
	UNUSED(offsetInBytes);

	auto rhiResource = StaticCast<Dx12Buffer>(pBuffer);
	assert(rhiResource);

	D3D12_BARRIER_SYNC syncAfter = DX12_BARRIER_SYNC(dstStage);

	D3D12_BARRIER_ACCESS accessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
	if (syncAfter == D3D12_BARRIER_SYNC_EXECUTE_INDIRECT)
	{
		accessAfter = D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
	}
	else if (syncAfter == D3D12_BARRIER_SYNC_COPY)
	{
		accessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
	}

	BarrierState barrier = BarrierState(syncAfter, accessAfter);
	m_Impl->TransitionBarrier(
		rhiResource.get(),
		barrier,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		bFlushImmediate
	);
}

void Dx12CommandContext::TransitionBufferToWrite(const Arc< render::Buffer >& pBuffer, render::ePipelineStage dstStage, u64 offsetInBytes, bool bFlushImmediate)
{
	UNUSED(offsetInBytes);

	auto rhiResource = StaticCast<Dx12Buffer>(pBuffer);
	assert(rhiResource);

	D3D12_BARRIER_SYNC syncAfter = DX12_BARRIER_SYNC(dstStage);

	BarrierState barrier = BarrierState(syncAfter, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS | D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
	m_Impl->TransitionBarrier(
		rhiResource.get(),
		barrier,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		bFlushImmediate
	);
}

void Dx12CommandContext::TransitionBarrier(const Arc< render::Texture >& pTexture, render::eTextureLayout newState, u32 subresource, bool bFlushImmediate)
{
	auto rhiTexture = StaticCast<Dx12Texture>(pTexture);
	assert(rhiTexture);

	m_Impl->TransitionBarrier(rhiTexture.get(), DX12_BARRIER_STATE(newState, IsComputeContext() || IsRaytracingContext()), subresource, bFlushImmediate);
}

void Dx12CommandContext::TransitionBarrier(Dx12Resource* pResource, const BarrierState& stateAfter, u32 subresource, bool bFlushImmediate)
{
	m_Impl->TransitionBarrier(pResource, stateAfter, subresource, bFlushImmediate);
}

void Dx12CommandContext::UAVBarrier(const Arc< render::Buffer >& pBuffer, bool bFlushImmediate)
{
	auto dx12Buffer = StaticCast<Dx12Buffer>(pBuffer);
	m_Impl->UAVBarrier(dx12Buffer.get(), bFlushImmediate);
}

void Dx12CommandContext::AliasingBarrier(Dx12Resource* pResourceBefore, Dx12Resource* pResourceAfter, bool bFlushImmediate)
{
	m_Impl->AliasingBarrier(pResourceBefore, pResourceAfter, bFlushImmediate);
}

void Dx12CommandContext::UploadData(const Arc< render::Buffer >& pDstBuffer, const void* pData, u32 numElements, u64 elemSizeInBytes, u64 dstOffsetInBytes)
{
	m_Impl->UploadData(StaticCast<Dx12Buffer>(pDstBuffer), pData, numElements, elemSizeInBytes, dstOffsetInBytes);
}

void Dx12CommandContext::CopyBuffer(ID3D12Resource2* d3d12DstBuffer, ID3D12Resource2* d3d12SrcBuffer, SIZE_T sizeInBytes, SIZE_T dstOffsetInBytes, SIZE_T srcOffsetInBytes)
{
	m_Impl->CopyBuffer(d3d12DstBuffer, d3d12SrcBuffer, sizeInBytes, dstOffsetInBytes, srcOffsetInBytes);
}

void Dx12CommandContext::CopyBuffer(const Arc< render::Buffer >& pDstBuffer, const Arc< render::Buffer >& pSrcBuffer, SIZE_T dstOffsetInBytes, SIZE_T srcOffsetInBytes)
{
	auto rhiBufferDst = StaticCast<Dx12Buffer>(pDstBuffer);
	auto rhiBufferSrc = StaticCast<Dx12Buffer>(pSrcBuffer);
	assert(rhiBufferDst && rhiBufferSrc);

	m_Impl->CopyBuffer(rhiBufferDst, rhiBufferSrc, rhiBufferSrc->SizeInBytes(), dstOffsetInBytes, srcOffsetInBytes);
}

void Dx12CommandContext::CopyBufferRegion(const Arc< render::Buffer >& pDstBuffer, const Arc< render::Buffer >& pSrcBuffer, u64 sizeInBytes, u64 dstOffsetInBytes, u64 srcOffsetInBytes)
{
	auto rhiBufferDst = StaticCast<Dx12Buffer>(pDstBuffer);
	auto rhiBufferSrc = StaticCast<Dx12Buffer>(pSrcBuffer);
	assert(rhiBufferDst && rhiBufferSrc);

	m_Impl->CopyBuffer(rhiBufferDst, rhiBufferSrc, sizeInBytes, dstOffsetInBytes, srcOffsetInBytes);
}

void Dx12CommandContext::CopyTexture(const Arc< render::Texture >& pDstTexture, const Arc< render::Texture >& pSrcTexture, u64 offsetInBytes)
{
	UNUSED(offsetInBytes);

	auto rhiTextureDst = StaticCast<Dx12Texture>(pDstTexture);
	auto rhiTextureSrc = StaticCast<Dx12Texture>(pSrcTexture);
	assert(rhiTextureDst && rhiTextureSrc);

	m_Impl->CopyTexture(rhiTextureDst, rhiTextureSrc);
}

void Dx12CommandContext::ResolveSubresource(Dx12Resource* pDstResource, Dx12Resource* pSrcResource, u32 dstSubresource, u32 srcSubresource)
{
	m_Impl->ResolveSubresource(pDstResource, pSrcResource, dstSubresource, srcSubresource);
}

void Dx12CommandContext::BuildBLAS(render::BottomLevelAccelerationStructure& blas)
{
	auto& dx12BLAS = static_cast<Dx12BottomLevelAS&>(blas);
	m_Impl->BuildBLAS(dx12BLAS);
}

void Dx12CommandContext::BuildTLAS(render::TopLevelAccelerationStructure& tlas)
{
	auto& dx12TLAS = static_cast<Dx12TopLevelAS&>(tlas);
	m_Impl->BuildTLAS(dx12TLAS);
}

void Dx12CommandContext::SetRenderPipeline(render::ComputePipeline* pRenderPipeline)
{
	auto rhiRenderPipeline = static_cast<Dx12ComputePipeline*>(pRenderPipeline);
	assert(rhiRenderPipeline);

	m_Impl->SetRenderPipeline(rhiRenderPipeline);
}

void Dx12CommandContext::SetRenderPipeline(render::GraphicsPipeline* pRenderPipeline)
{
	auto rhiRenderPipeline = static_cast<Dx12GraphicsPipeline*>(pRenderPipeline);
	assert(rhiRenderPipeline);

	m_Impl->SetRenderPipeline(rhiRenderPipeline);
}

void Dx12CommandContext::SetRenderPipeline(render::RaytracingPipeline* pRenderPipeline)
{
	auto rhiRenderPipeline = static_cast<Dx12RaytracingPipeline*>(pRenderPipeline);
	assert(rhiRenderPipeline);

	m_Impl->SetRenderPipeline(rhiRenderPipeline);
}

void Dx12CommandContext::SetConstants(u32 sizeInBytes, const void* pData, render::eShaderStage stage, u32 offsetInBytes)
{
	if (stage & render::eShaderStage::Compute)
		m_Impl->SetComputeRootConstants(sizeInBytes, pData, offsetInBytes);
	else
		m_Impl->SetGraphicsRootConstants(sizeInBytes, pData, offsetInBytes);
}

void Dx12CommandContext::SetComputeConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes)
{
	m_Impl->SetComputeRootConstants(sizeInBytes, pData, offsetInBytes);
}

void Dx12CommandContext::SetGraphicsConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes)
{
	m_Impl->SetGraphicsRootConstants(sizeInBytes, pData, offsetInBytes);
}

void Dx12CommandContext::SetComputeDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData)
{
	m_Impl->SetComputeDynamicConstantBuffer(name, sizeInBytes, pData);
}

void Dx12CommandContext::SetGraphicsDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData)
{
	m_Impl->SetGraphicsDynamicConstantBuffer(name, sizeInBytes, pData);
}

void Dx12CommandContext::SetComputeShaderResource(const std::string& name, Arc< render::Buffer > pBuffer)
{
	auto rhiBuffer = StaticCast<Dx12Buffer>(pBuffer);
	assert(rhiBuffer);

	const auto& state = rhiBuffer->GetCurrentState();
	bool bIsUAV = state.GetSubresourceState() == BarrierStates::BufferUnorderedAccess;

	if (bIsUAV)
		m_Impl->SetComputeUnorderedAccessView(name, rhiBuffer->GpuAddress());
	else
		m_Impl->SetComputeShaderResourceView(name, rhiBuffer->GpuAddress());
}

void Dx12CommandContext::SetComputeShaderResource(const std::string& name, Arc< render::Texture > pTexture, Arc< render::Sampler > pSamplerInCharge)
{
	UNUSED(pSamplerInCharge);

	auto rhiTexture = StaticCast<Dx12Texture>(pTexture);
	assert(rhiTexture);

	const auto& state = rhiTexture->GetCurrentState();
	bool bIsUAV = state.GetSubresourceState() == BarrierStates::NonPixelShaderResource;

	if (bIsUAV)
		m_Impl->SetComputeUnorderedAccessView(name, rhiTexture->GpuAddress());
	else
		m_Impl->SetComputeShaderResourceView(name, rhiTexture->GpuAddress());
}

void Dx12CommandContext::SetGraphicsShaderResource(const std::string& name, Arc< render::Texture > pTexture, Arc< render::Sampler > pSamplerInCharge)
{
	UNUSED(pSamplerInCharge);

	auto rhiTexture = StaticCast<Dx12Texture>(pTexture);
	assert(rhiTexture);

	m_Impl->SetGraphicsShaderResourceView(name, rhiTexture->GpuAddress());
}

void Dx12CommandContext::SetGraphicsShaderResource(const std::string& name, Arc< render::Buffer > pBuffer)
{
	auto rhiBuffer = StaticCast<Dx12Buffer>(pBuffer);
	assert(rhiBuffer);

	if (rhiBuffer->GetType() == eBufferType::Structured)
		m_Impl->SetGraphicsShaderResourceView(name, StaticCast<Dx12StructuredBuffer>(rhiBuffer)->GpuAddress());
	else
		m_Impl->SetGraphicsConstantBufferView(name, rhiBuffer->GpuAddress());
}

void Dx12CommandContext::SetComputeConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv)
{
	m_Impl->SetComputeConstantBufferView(name, srv);
}

void Dx12CommandContext::SetGraphicsConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv)
{
	m_Impl->SetGraphicsConstantBufferView(name, srv);
}

void Dx12CommandContext::SetComputeShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv)
{
	m_Impl->SetComputeShaderResourceView(name, srv);
}

void Dx12CommandContext::SetGraphicsShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv)
{
	m_Impl->SetGraphicsShaderResourceView(name, srv);
}

void Dx12CommandContext::SetAccelerationStructure(const std::string& name, render::TopLevelAccelerationStructure& tlas)
{
	auto& dx12TLAS = static_cast<Dx12TopLevelAS&>(tlas);
	m_Impl->SetAccelerationStructureSRV(name, dx12TLAS.GetGPUVirtualAddress());
}

void Dx12CommandContext::StageDescriptor(const std::string& name, Arc< render::Buffer > pBuffer, u32 offset)
{
	UNUSED(offset);

	auto rhiBuffer = StaticCast<Dx12StructuredBuffer>(pBuffer);
	assert(rhiBuffer);

	m_Impl->StageDescriptor(name, rhiBuffer);
}

void Dx12CommandContext::StageDescriptor(const std::string& name, Arc< render::Texture > pTexture, Arc< render::Sampler > pSamplerInCharge, u32 offset)
{
	UNUSED(offset);
	UNUSED(pSamplerInCharge);

	auto rhiTexture = StaticCast<Dx12Texture>(pTexture);
	assert(rhiTexture);

	m_Impl->StageDescriptor(name, rhiTexture);
}

void Dx12CommandContext::StageDescriptorMip(const std::string& name, Arc< render::Texture > pTexture, u32 mipLevel, Arc< render::Sampler > pSamplerInCharge)
{
	UNUSED(pSamplerInCharge);

	auto rhiTexture = StaticCast<Dx12Texture>(pTexture);
	assert(rhiTexture);

	// With sampler: SRV read of a specific mip.
	// Without sampler: UAV write via per-mip UAV descriptor.
	if (pSamplerInCharge)
		m_Impl->StageDescriptor(name, rhiTexture);
	else
		m_Impl->StageDescriptorMip(name, rhiTexture, mipLevel);
}

void Dx12CommandContext::StageDescriptors(std::vector< std::pair< std::string, u32 > >&& srcHandles, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	m_Impl->StageDescriptors(std::move(srcHandles), heapType);
}

void Dx12CommandContext::SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps)
{
	m_Impl->SetDescriptorHeaps(d3d12DescriptorHeaps);
}

void Dx12CommandContext::SetRenderTarget(u32 numRenderTargets, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
	m_Impl->SetRenderTarget(numRenderTargets, rtv, dsv);
}

void Dx12CommandContext::BeginRenderPass(Arc< render::RenderTarget > pRenderTarget)
{
	auto rhiRenderTarget = StaticCast<Dx12RenderTarget>(pRenderTarget);
	assert(rhiRenderTarget);

	rhiRenderTarget->ClearTexture(*this, render::eAttachmentPoint::All);

	m_Impl->BeginRenderPass(rhiRenderTarget);
}

void Dx12CommandContext::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
	m_Impl->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void Dx12CommandContext::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
	m_Impl->DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void Dx12CommandContext::DrawMeshTasksIndirect(const Arc< render::Buffer >& pArgumentBuffer, u64 offsetInBytes, u32 numDraws, u32 strideInBytes)
{
	UNUSED(strideInBytes);
	m_Impl->DrawIndirect(StaticCast<Dx12Buffer>(pArgumentBuffer), offsetInBytes, numDraws);
}

void Dx12CommandContext::DrawMeshTasksIndirectCount(const Arc< render::Buffer >& pArgumentBuffer, u64 offsetInBytes, const Arc< render::Buffer >& pCountBuffer, u32 numDraws, u32 strideInBytes)
{
	UNUSED(strideInBytes);
	m_Impl->DrawIndirectWithCount(StaticCast<Dx12Buffer>(pArgumentBuffer), offsetInBytes, StaticCast<Dx12Buffer>(pCountBuffer), numDraws);
}

void Dx12CommandContext::Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	m_Impl->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void Dx12CommandContext::DispatchRays(render::ShaderBindingTable& sbt, u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	auto& rhiSBT = static_cast<Dx12ShaderBindingTable&>(sbt);
	m_Impl->DispatchRays(rhiSBT, numGroupsX, numGroupsY, numGroupsZ);
}

double Dx12CommandContext::GetLastFrameElapsedTime() const
{
	return m_Impl->GetLastFrameElapsedTime();
}

void Dx12CommandContext::BeginGpuMarker(const char* name, bool bWithStats)
{
	m_Impl->BeginGpuMarker(name, bWithStats);
}

void Dx12CommandContext::EndGpuMarker()
{
	m_Impl->EndGpuMarker();
}

const std::vector< render::GpuProfileEntry >& Dx12CommandContext::GetLastFrameProfile() const
{
	return m_Impl->GetLastFrameProfile();
}

bool Dx12CommandContext::IsComputeContext() const
{
	return m_Impl->IsComputeContext();
}

bool Dx12CommandContext::IsGraphicsContext() const
{
	return m_Impl->IsGraphicsContext();
}

bool Dx12CommandContext::IsRaytracingContext() const
{
	return m_Impl->IsRaytracingContext();
}

D3D12_COMMAND_LIST_TYPE Dx12CommandContext::GetCommandListType() const
{
	return m_Impl->GetCommandListType();
}

ID3D12GraphicsCommandList10* Dx12CommandContext::GetD3D12CommandList() const
{
	return m_Impl->GetD3D12CommandList();
}

}
