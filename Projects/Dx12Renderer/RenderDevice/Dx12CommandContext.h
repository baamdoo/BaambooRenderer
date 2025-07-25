#pragma once

namespace dx12
{

constexpr u32 MAX_NUM_PENDING_BARRIERS = 16;

class RenderTarget;
class VertexBuffer;
class IndexBuffer;
class RootSignature;
class DescriptorHeap;
class GraphicsPipeline;
class ComputePipeline;
class DynamicBufferAllocator;
class Buffer;
class Texture;
class SceneResource;

class SyncObject
{
public:
	SyncObject(u64 fenceValue, CommandQueue& cmdQueue) : m_FenceValue(fenceValue), m_CommandQueue(cmdQueue) {}
	void Wait();

private:
	u64           m_FenceValue;
	CommandQueue& m_CommandQueue;
};

class CommandContext
{
public:
	CommandContext(RenderDevice& device, D3D12_COMMAND_LIST_TYPE type);
	~CommandContext();

	void Open();
	void Close();
	SyncObject Execute();

	void TransitionBarrier(const Arc< Resource >& pResource, D3D12_RESOURCE_STATES stateAfter, u32 subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool bFlushImmediate = true);
	void UAVBarrier(const Arc< Resource >& pResource, bool bFlushImmediate = false);
	void AliasingBarrier(const Arc< Resource >& pResourceBefore, const Arc< Resource >& pResourceAfter, bool bFlushImmediate = false);

	void CopyBuffer(const Arc< Buffer >& pDstBuffer, const Arc< Buffer >& pSrcBuffer, size_t sizeInBytes);
	void CopyBuffer(ID3D12Resource* d3d12DstBuffer, ID3D12Resource* d3d12SrcBuffer, SIZE_T sizeInBytes);
	void CopyTexture(const Arc< Texture >& pDstTexture, const Arc< Texture >& pSrcTexture);
	void ResolveSubresource(const Arc< Resource >& pDstResource, const Arc< Resource >& pSrcResource, u32 dstSubresource = 0, u32 srcSubresource = 0);

	void SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology);

	void ClearTexture(const Arc< Texture >& pTexture);
	void ClearDepthStencilTexture(const Arc< Texture >& pTexture, D3D12_CLEAR_FLAGS clearFlags);

	void SetViewport(const D3D12_VIEWPORT& viewport);
	void SetViewports(const std::vector< D3D12_VIEWPORT >& viewports);

	void SetScissorRect(const D3D12_RECT& scissorRect);
	void SetScissorRects(const std::vector< D3D12_RECT >& scissorRects);

	void SetRenderPipeline(GraphicsPipeline* pGraphicsPipelineState);
	void SetRenderPipeline(ComputePipeline* pComputePipelineState);

	void SetGraphicsRootSignature(RootSignature* pRootSignature);
	void SetComputeRootSignature(RootSignature* pRootSignature);

	void SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps);

	void SetRenderTarget(u32 numRenderTargets, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv = D3D12_CPU_DESCRIPTOR_HANDLE());
	void SetRenderTarget(const RenderTarget& renderTarget);

	void SetGraphicsRootConstant(u32 rootIndex, u32 srcValue, u32 dstOffset = 0);
	void SetGraphicsRootConstants(u32 rootIndex, u32 srcSizeInBytes, void* srcData, u32 dstOffsetInBytes = 0);
	void SetComputeRootConstant(u32 rootIndex, u32 srcValue, u32 dstOffset = 0);
	void SetComputeRootConstants(u32 rootIndex, u32 srcSizeInBytes, void* srcData, u32 dstOffsetInBytes = 0);

	void SetGraphicsDynamicConstantBuffer(u32 rootIndex, size_t sizeInBytes, const void* pData);
	template< typename T >
	void SetGraphicsDynamicConstantBuffer(u32 rootIndex, const T& data)
	{
		SetGraphicsDynamicConstantBuffer(rootIndex, sizeof(T), &data);
	}
	void SetComputeDynamicConstantBuffer(u32 rootIndex, size_t sizeInBytes, const void* pData);
	template< typename T >
	void SetComputeDynamicConstantBuffer(u32 rootIndex, const T& data)
	{
		SetComputeDynamicConstantBuffer(rootIndex, sizeof(T), &data);
	}

	void SetGraphicsConstantBufferView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetGraphicsShaderResourceView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetComputeConstantBufferView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetComputeShaderResourceView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetComputeUnorderedAccessView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);

	void StageDescriptors(
		u32 rootIndex,
		u32 numDescriptors,
		u32 offset,
		D3D12_CPU_DESCRIPTOR_HANDLE srcHandle,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	void StageDescriptors(
		u32 rootIndex,
		u32 offset,
		std::vector< D3D12_CPU_DESCRIPTOR_HANDLE >&& srcHandles,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	void Draw(u32 vertexCount, u32 instanceCount = 1, u32 startVertex = 0, u32 startInstance = 0);
	void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 startIndex = 0, u32 baseVertex = 0, u32 startInstance = 0);
	void DrawIndexedIndirect(const SceneResource& sceneResource);

	void Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ);

	template< u32 numThreadsPerGroupX >
	void Dispatch1D(u32 numThreadsX)
	{
		u32 numGroupsX = RoundUpAndDivide(numThreadsX, numThreadsPerGroupX);
		Dispatch(numGroupsX, 1, 1);
	}

	template< u32 numThreadsPerGroupX, u32 numThreadsPerGroupY >
	void Dispatch2D(u32 numThreadsX, u32 numThreadsY)
	{
		u32 numGroupsX = RoundUpAndDivide(numThreadsX, numThreadsPerGroupX);
		u32 numGroupsY = RoundUpAndDivide(numThreadsY, numThreadsPerGroupY);
		Dispatch(numGroupsX, numGroupsY, 1);
	}

	template< u32 numThreadsPerGroupX, u32 numThreadsPerGroupY, u32 numThreadsPerGroupZ >
	void Dispatch3D(u32 numThreadsX, u32 numThreadsY, u32 numThreadsZ)
	{
		u32 numGroupsX = RoundUpAndDivide(numThreadsX, numThreadsPerGroupX);
		u32 numGroupsY = RoundUpAndDivide(numThreadsY, numThreadsPerGroupY);
		u32 numGroupsZ = RoundUpAndDivide(numThreadsZ, numThreadsPerGroupZ);
		Dispatch(numGroupsX, numGroupsY, numGroupsZ);
	}

private:
	void AddBarrier(const D3D12_RESOURCE_BARRIER& barrier, bool bFlushImmediate);
	void FlushResourceBarriers();

	void BindDescriptorHeaps();

public:
	D3D12_COMMAND_LIST_TYPE GetCommandListType() const { return m_Type; }
	ID3D12GraphicsCommandList2* GetD3D12CommandList() const { return m_d3d12CommandList; }

	template< typename T >
	constexpr T RoundUpAndDivide(T Value, size_t Alignment)
	{
		return (T)((Value + Alignment - 1) / Alignment);
	}

private:
	RenderDevice&           m_RenderDevice;
	D3D12_COMMAND_LIST_TYPE m_Type = {};
	
	DynamicBufferAllocator* m_pDynamicBufferAllocator = nullptr;

	ID3D12GraphicsCommandList2* m_d3d12CommandList      = nullptr;
	ID3D12CommandAllocator*     m_d3d12CommandAllocator = nullptr;

	RootSignature* m_pRootSignature = nullptr;

	GraphicsPipeline* m_pGraphicsPipeline = nullptr;
	ComputePipeline*  m_pComputePipeline = nullptr;

	D3D_PRIMITIVE_TOPOLOGY m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	DescriptorHeap*       m_pDescriptorHeaps[NUM_RESOURCE_DESCRIPTOR_TYPE]               = {};
	ID3D12DescriptorHeap* m_CurrentDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

	u32                    m_NumBarriersToFlush                         = 0;
	D3D12_RESOURCE_BARRIER m_ResourceBarriers[MAX_NUM_PENDING_BARRIERS] = {};
};

}