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
	Dx12CommandContext(Dx12RenderDevice& rd, D3D12_COMMAND_LIST_TYPE type);
	virtual ~Dx12CommandContext() = default;

	void Open();
	void Close();

	void ClearTexture(const Arc< Dx12Texture >& pTexture);
	void ClearDepthStencilTexture(const Arc< Dx12Texture >& pTexture, D3D12_CLEAR_FLAGS clearFlags);

	virtual void TransitionBarrier(Arc< render::Texture > pTexture, render::eTextureLayout newState, u32 subresource = ALL_SUBRESOURCES, bool bFlushImmediate = false) override;
	void TransitionBarrier(Dx12Resource* pResource, D3D12_RESOURCE_STATES stateAfter, u32 subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool bFlushImmediate = true);
	void UAVBarrier(Dx12Resource* pResource, bool bFlushImmediate = false);
	void AliasingBarrier(Dx12Resource* pResourceBefore, Dx12Resource* pResourceAfter, bool bFlushImmediate = false);

	void CopyBuffer(ID3D12Resource* d3d12DstBuffer, ID3D12Resource* d3d12SrcBuffer, SIZE_T sizeInBytes, SIZE_T dstOffsetInBytes);
	virtual void CopyBuffer(Arc< render::Buffer > pDstBuffer, Arc< render::Buffer > pSrcBuffer, u64 offsetInBytes = 0) override;
	virtual void CopyTexture(Arc< render::Texture > pDstTexture, Arc< render::Texture > pSrcTexture, u64 offsetInBytes = 0) override;
	void ResolveSubresource(Dx12Resource* pDstResource, Dx12Resource* pSrcResource, u32 dstSubresource = 0, u32 srcSubresource = 0);

	virtual void SetRenderPipeline(render::ComputePipeline* pRenderPipeline) override;
	virtual void SetRenderPipeline(render::GraphicsPipeline* pRenderPipeline) override;

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

	virtual void StageDescriptor(const std::string& name, Arc< render::Buffer > pBuffer, u32 offset = 0) override;
	virtual void StageDescriptor(const std::string& name, Arc< render::Texture > pTexture, Arc< render::Sampler > pSamplerInCharge, u32 offset = 0) override;
	void StageDescriptors(
		std::vector< std::pair< std::string, D3D12_CPU_DESCRIPTOR_HANDLE > > && srcHandles,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	void StageDescriptors(
		u32 rootIndex,
		u32 offset,
		std::vector< D3D12_CPU_DESCRIPTOR_HANDLE >&& srcHandles,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	void SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps);

	void SetRenderTarget(u32 numRenderTargets, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv = D3D12_CPU_DESCRIPTOR_HANDLE());
	virtual void BeginRenderPass(Arc< render::RenderTarget > pRenderTarget) override;
	virtual void EndRenderPass() override {}

	virtual void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) override;
	virtual void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0) override;
	virtual void DrawScene(const render::SceneResource& sceneResource) override;
	virtual void Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ) override;

public:
	bool IsComputeContext() const;
	bool IsGraphicsContext() const;

	D3D12_COMMAND_LIST_TYPE GetCommandListType() const;
	ID3D12GraphicsCommandList2* GetD3D12CommandList() const;

private:
	class Impl;
	Box< Impl > m_Impl;
};

}