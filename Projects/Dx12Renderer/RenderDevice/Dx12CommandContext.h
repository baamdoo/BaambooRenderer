#pragma once
#include "RenderCommon/CommandContext.h"

namespace dx12
{

constexpr u32 MAX_NUM_PENDING_BARRIERS = 16;

class Dx12Resource;
class Dx12Buffer;
class Dx12Texture;
class Dx12RenderTarget;

class Dx12CommandContext : public render::CommandContext
{
public:
	Dx12CommandContext(Dx12RenderDevice& rd, const Dx12CommandQueue& cq, D3D12_COMMAND_LIST_TYPE type);
	virtual ~Dx12CommandContext() = default;

	void Open();
	void Close();

	// ---- Clear ----
	virtual void ClearBuffer(const Arc< render::Buffer >& pBuffer, u32 value, u64 offsetInBytes = 0) override;
	virtual void ClearTexture(const Arc< render::Texture >& pTexture, render::eTextureLayout newLayout) override;
	void ClearRenderTarget(const Arc< Dx12Texture >& pTexture);
	void ClearDepthStencil(const Arc< Dx12Texture >& pTexture, D3D12_CLEAR_FLAGS clearFlags);

	// ---- Barrier ----
	virtual void TransitionBufferToRead(const Arc< render::Buffer >& pBuffer, render::ePipelineStage dstStage, u64 offsetInBytes = 0, bool bFlushImmediate = false) override;
	virtual void TransitionBufferToWrite(const Arc< render::Buffer >& pBuffer, render::ePipelineStage dstStage, u64 offsetInBytes = 0, bool bFlushImmediate = false) override;
	virtual void TransitionBarrier(const Arc< render::Texture >& pTexture, render::eTextureLayout newState, u32 subresource = ALL_SUBRESOURCES, bool bFlushImmediate = false) override;
	virtual void UAVBarrier(const Arc< render::Buffer >& pBuffer, bool bFlushImmediate) override;

	void TransitionBarrier(Dx12Resource* pResource, const struct BarrierState& stateAfter, u32 subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool bFlushImmediate = true);
	void AliasingBarrier(Dx12Resource* pResourceBefore, Dx12Resource* pResourceAfter, bool bFlushImmediate = false);

	// ---- Copy ----
	void UploadData(const Arc< render::Buffer >& pDstBuffer, const void* pData, u32 numElements, u64 elemSizeInBytes, u64 dstOffsetInBytes);

	virtual void CopyBuffer(const Arc< render::Buffer >& pDstBuffer, const Arc< render::Buffer >& pSrcBuffer, SIZE_T dstOffsetInBytes = 0, SIZE_T srcOffsetInBytes = 0) override;
	virtual void CopyTexture(const Arc< render::Texture >& pDstTexture, const Arc< render::Texture >& pSrcTexture, u64 offsetInBytes = 0) override;
	void CopyBuffer(ID3D12Resource2* d3d12DstBuffer, ID3D12Resource2* d3d12SrcBuffer, SIZE_T sizeInBytes, SIZE_T dstOffsetInBytes, SIZE_T srcOffsetInBytes);
	void ResolveSubresource(Dx12Resource* pDstResource, Dx12Resource* pSrcResource, u32 dstSubresource = 0, u32 srcSubresource = 0);

	// ---- Acceleration Structure ----
	virtual void BuildBLAS(render::BottomLevelAccelerationStructure& blas) override;
	virtual void BuildTLAS(render::TopLevelAccelerationStructure& tlas) override;

	// ---- Pipeline ----
	virtual void SetRenderPipeline(render::ComputePipeline* pRenderPipeline) override;
	virtual void SetRenderPipeline(render::GraphicsPipeline* pRenderPipeline) override;
	virtual void SetRenderPipeline(render::RaytracingPipeline* pRenderPipeline) override;

	// ---- Bindings ----
	virtual void SetComputeConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes = 0) override;
	virtual void SetGraphicsConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes = 0) override;

	virtual void SetComputeDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData) override;
	virtual void SetGraphicsDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData) override;

	virtual void SetComputeShaderResource(const std::string& name, Arc< render::Texture > pTexture, Arc< render::Sampler > pSamplerInCharge) override;
	virtual void SetGraphicsShaderResource(const std::string& name, Arc< render::Texture > pTexture, Arc< render::Sampler > pSamplerInCharge) override;
	virtual void SetComputeShaderResource(const std::string& name, Arc< render::Buffer > pBuffer) override;
	virtual void SetGraphicsShaderResource(const std::string& name, Arc< render::Buffer > pBuffer) override;

	void SetComputeConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv);
	void SetGraphicsConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv);
	void SetComputeShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv);
	void SetGraphicsShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv);

	virtual void SetAccelerationStructure(const std::string& name, render::TopLevelAccelerationStructure& tlas) override;

	virtual void StageDescriptor(const std::string& name, Arc< render::Buffer > pBuffer, u32 offset = 0) override;
	virtual void StageDescriptor(const std::string& name, Arc< render::Texture > pTexture, Arc< render::Sampler > pSamplerInCharge, u32 offset = 0) override;
	void StageDescriptors(
		std::vector< std::pair< std::string, u32 > > && srcHandles,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	void SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps);

	// ---- Render Target ----
	void SetRenderTarget(u32 numRenderTargets, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv = D3D12_CPU_DESCRIPTOR_HANDLE());
	virtual void BeginRenderPass(Arc< render::RenderTarget > pRenderTarget) override;
	virtual void EndRenderPass() override {}

	// ---- Draw / Dispatch ----
	virtual void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) override;
	virtual void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0) override;
	virtual void DrawMeshTasksIndirect(const Arc< render::Buffer >& pArgumentBuffer, u64 offsetInBytes, u32 numDraws, u32 strideInBytes) override;
	virtual void DrawMeshTasksIndirectCount(const Arc< render::Buffer >& pArgumentBuffer, u64 offsetInBytes, const Arc< render::Buffer >& pCountBuffer, u32 numDraws, u32 strideInBytes) override;

	virtual void Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ) override;
	virtual void DispatchRays(render::ShaderBindingTable& sbt, u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ = 1) override;

	virtual double GetLastFrameElapsedTime() const override;

public:
	bool IsComputeContext() const;
	bool IsGraphicsContext() const;
	bool IsRaytracingContext() const;

	D3D12_COMMAND_LIST_TYPE GetCommandListType() const;
	ID3D12GraphicsCommandList10* GetD3D12CommandList() const;

private:
	class Impl;
	Box< Impl > m_Impl;
};

}