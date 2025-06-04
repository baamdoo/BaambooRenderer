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

class CommandContext
{
public:
	CommandContext(RenderDevice& device, D3D12_COMMAND_LIST_TYPE type);
	~CommandContext();

	void Open();
	void Close();

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

	void SetPipelineState(GraphicsPipeline* pGraphicsPipelineState);
	void SetPipelineState(ComputePipeline* pComputePipelineState);

	void SetGraphicsRootSignature(RootSignature* pRootSignature);
	void SetComputeRootSignature(RootSignature* pRootSignature);

	void SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps);

	void SetRenderTarget(const RenderTarget& renderTarget);

	void SetGraphics32BitConstant(u32 rootIndex, u32 srcValue, u32 dstOffset = 0);
	void SetGraphics32BitConstants(u32 rootIndex, u32 srcSizeInBytes, void* srcData, u32 dstOffsetInBytes = 0);

	void SetGraphicsDynamicConstantBuffer(u32 rootIndex, size_t sizeInBytes, const void* bufferData);
	template< typename T >
	void SetGraphicsDynamicConstantBuffer(u32 rootIndex, const T& data)
	{
		SetGraphicsDynamicConstantBuffer(rootIndex, sizeof(T), &data);
	}

	void SetGraphicsShaderResourceView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);

	void StageDescriptors(
		u32 rootIndex,
		u32 numDescriptors,
		u32 offset,
		D3D12_CPU_DESCRIPTOR_HANDLE srcHandle,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t startVertex = 0, uint32_t startInstance = 0);
	void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t startIndex = 0, int32_t baseVertex = 0, uint32_t startInstance = 0);
	void DrawIndexedIndirect(const SceneResource& sceneResource);

	void Dispatch(uint32_t numGroupsX, uint32_t numGroupsY = 1, uint32_t numGroupsZ = 1);

private:
	void AddBarrier(const D3D12_RESOURCE_BARRIER& barrier, bool bFlushImmediate);
	void FlushResourceBarriers();

	void BindDescriptorHeaps();

public:
	D3D12_COMMAND_LIST_TYPE GetCommandListType() const { return m_Type; }
	ID3D12GraphicsCommandList2* GetD3D12CommandList() const { return m_d3d12CommandList; }

private:
	RenderDevice&           m_RenderDevice;
	D3D12_COMMAND_LIST_TYPE m_Type = {};
	
	DynamicBufferAllocator* m_pDynamicBufferAllocator = nullptr;

	ID3D12GraphicsCommandList2* m_d3d12CommandList = nullptr;
	ID3D12CommandAllocator* m_d3d12CommandAllocator = nullptr;

	RootSignature* m_pRootSignature = nullptr;

	GraphicsPipeline* m_pGraphicsPipeline = nullptr;
	ComputePipeline* m_pComputePipeline = nullptr;

	D3D_PRIMITIVE_TOPOLOGY m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	DescriptorHeap* m_pDescriptorHeaps[NUM_RESOURCE_DESCRIPTOR_TYPE] = {};
	ID3D12DescriptorHeap* m_CurrentDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

	u32 m_NumBarriersToFlush = 0;
	D3D12_RESOURCE_BARRIER m_ResourceBarriers[MAX_NUM_PENDING_BARRIERS] = {};
};

}